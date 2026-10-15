"""Inspect a kernel ELF offline; never execute it or contact a console."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def inspect(path, dump_ranges=(), export_directory=None):
    original = path.read_bytes()
    data = bytearray(original)
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("Expected a little-endian ELF64 image")
    entry, phoff = struct.unpack_from("<QQ", data, 24)
    phsize, phcount = struct.unpack_from("<HH", data, 54)
    if phsize != 56 or phoff + phcount * phsize > len(data):
        raise ValueError("Invalid program header table")
    segments = []
    dynamic = {}
    for index in range(phcount):
        kind, flags, offset, va, _, filesz, memsz, _ = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + index * phsize)
        if kind == 1:
            if offset + filesz > len(data):
                raise ValueError("Truncated load segment")
            segments.append(dict(flags=flags, offset=offset, va=va,
                                 filesz=filesz, memsz=memsz))
        if kind == 2 and offset + filesz <= len(data):
            for at in range(offset, offset + filesz - 15, 16):
                tag, value = struct.unpack_from("<QQ", data, at)
                if tag == 0:
                    break
                dynamic[tag] = value

    def offset_of(va, size=1):
        for segment in segments:
            relative = va - segment["va"]
            if 0 <= relative and relative + size <= segment["filesz"]:
                return segment["offset"] + relative
        return None

    def va_of(offset):
        for segment in segments:
            if segment["offset"] <= offset < segment["offset"] + segment["filesz"]:
                return segment["va"] + offset - segment["offset"]
        return None

    # Reconstruct relative pointers in the local copy at the ELF's link
    # addresses. This is data processing, not loading or executing the kernel.
    relocations = 0
    if dynamic.get(9) == 24 and dynamic.get(8, 0) % 24 == 0:
        start = offset_of(dynamic.get(7, 0), dynamic.get(8, 0))
        if start is not None:
            for at in range(start, start + dynamic[8], 24):
                target, info, addend = struct.unpack_from("<QQQ", original, at)
                destination = offset_of(target, 8)
                if info == 8 and destination is not None:
                    struct.pack_into("<Q", data, destination, addend)
                    relocations += 1

    classes = []
    search = 0
    while True:
        location = data.find(b"btts\x00", search)
        if location < 0:
            break
        search = location + 1
        name_va = va_of(location)
        if name_va is None:
            continue
        reference = 0
        while True:
            reference = data.find(struct.pack("<Q", name_va), reference)
            if reference < 0:
                break
            candidate = reference
            reference += 1
            if candidate % 8 or candidate + 48 > len(data):
                continue
            _, methods_va, size, bases, refs, ops = struct.unpack_from("<6Q", data, candidate)
            methods_offset = offset_of(methods_va, 16)
            if methods_offset is None or not 0 <= size < 1024 * 1024:
                continue
            methods = []
            for index in range(32):
                at = offset_of(methods_va + index * 16, 16)
                if at is None:
                    break
                descriptor, function = struct.unpack_from("<QQ", data, at)
                if descriptor == function == 0:
                    break
                if offset_of(descriptor, 8) is None or offset_of(function, 1) is None:
                    methods = []
                    break
                implementation = function
                visited = set()
                for _ in range(8):
                    entry_offset = offset_of(implementation, 5)
                    if entry_offset is None or implementation in visited or data[entry_offset] != 0xE9:
                        break
                    visited.add(implementation)
                    target = implementation + 5 + struct.unpack_from("<i", data, entry_offset + 1)[0]
                    if offset_of(target, 1) is None:
                        break
                    implementation = target
                methods.append(dict(index=index, descriptor=hex(descriptor),
                                    function=hex(function), file_offset=hex(offset_of(function)),
                                    implementation=hex(implementation),
                                    implementation_offset=hex(offset_of(implementation))))
            if methods:
                classes.append(dict(address=hex(va_of(candidate)), file_offset=hex(candidate),
                                    softc_size=size, methods=methods))
    for address, size in dump_ranges:
        start = offset_of(address, size)
        if start is None or not 0 < size <= 65536 or export_directory is None:
            raise ValueError("Requested range must be file-backed, at most 64 KiB, and have an export directory")
        export_directory.mkdir(parents=True, exist_ok=True)
        (export_directory / f"range-{address:016x}.bin").write_bytes(data[start:start + size])
    return dict(path=str(path), sha256=hashlib.sha256(original).hexdigest(),
                entry=hex(entry), load_segments=segments,
                reconstructed_relative_pointers=relocations,
                btts_class_candidates=classes)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--export-methods", type=Path,
                        help="write up to 2 KiB of each candidate method for offline disassembly")
    parser.add_argument("--dump-range", action="append", default=[], metavar="ADDRESS:SIZE",
                        help="export a file-backed virtual range, with reconstructed pointers, into the export directory")
    args = parser.parse_args()
    ranges = [tuple(int(part, 0) for part in spec.split(":")) for spec in args.dump_range]
    if any(len(spec) != 2 for spec in ranges):
        parser.error("--dump-range requires ADDRESS:SIZE")
    metadata = inspect(args.elf, ranges, args.export_methods)
    report = json.dumps(metadata, indent=2)
    if args.export_methods:
        args.export_methods.mkdir(parents=True, exist_ok=True)
        image = args.elf.read_bytes()
        for candidate_index, candidate in enumerate(metadata["btts_class_candidates"]):
            for method in candidate["methods"]:
                start = int(method["implementation_offset"], 16)
                segment = next(s for s in metadata["load_segments"]
                               if s["offset"] <= start < s["offset"] + s["filesz"])
                stop = min(start + 2048, segment["offset"] + segment["filesz"])
                destination = args.export_methods / f"btts-{candidate_index}-method-{method['index']}.bin"
                destination.write_bytes(image[start:stop])
    if args.output:
        args.output.write_text(report + "\n", encoding="utf-8")
    print(report)
