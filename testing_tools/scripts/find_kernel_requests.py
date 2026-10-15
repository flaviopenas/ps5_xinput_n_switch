"""Find native request constants in executable ELF segments, offline only.

Byte matches are candidates, not verified instructions or firmware-compatible
addresses. Export bounded windows for manual disassembly.
"""
import argparse
import json
import re
from pathlib import Path
import struct
import subprocess

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", type=Path, default=Path("testing_tools/build/native-requests"))
    parser.add_argument("--strings", action="store_true", help="Inventory Bluetooth-related printable strings")
    parser.add_argument("--call-target", action="append", default=[], type=lambda value: int(value, 0),
                        help="Find candidate x86 rel32 calls/jumps to an offline address")
    parser.add_argument("--hci", action="store_true", help="Find candidate mov esi,imm32 BLE scan/connect opcodes")
    parser.add_argument("--request", action="append", type=lambda value: int(value, 0),
                        help="Search these 32-bit constants instead of the default request list")
    args = parser.parse_args()
    data = args.elf.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        parser.error("Expected little-endian ELF64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    stride, count = struct.unpack_from("<HH", data, 54)
    if stride != 56 or phoff + stride * count > len(data):
        parser.error("Invalid program headers")
    args.output.mkdir(parents=True, exist_ok=True)
    hits = []
    strings = []
    executable_segments = []
    requests = args.request or [0x8004622B, 0x8010622E, 0x8010622F, 0x8010A40B, 0x80206230, 0xC0106206]
    if any(value < 0 or value > 0xffffffff for value in requests):
        parser.error("--request must fit in an unsigned 32-bit value")
    for index in range(count):
        kind, flags, offset, va, _, size, _, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + index * stride)
        if kind != 1:
            continue
        if offset + size > len(data):
            parser.error("Truncated executable segment")
        segment = data[offset:offset + size]
        if args.strings:
            for match in re.finditer(rb"[\x20-\x7e]{4,}\x00", segment):
                value = match.group()[:-1].decode("ascii")
                if re.search(r"bluetooth|\bbtts|\bbthid|\bbt_|/dev/bt\b|sceBt", value, re.I):
                    strings.append(dict(address=hex(va+match.start()), text=value))
        if not flags & 1:
            continue
        executable_segments.append((va, segment))
        for request in requests:
            position = 0
            while True:
                at = segment.find(struct.pack("<I", request), position)
                if at < 0:
                    break
                position = at + 1
                start, end = max(0, at - 128), min(size, at + 1024)
                path = args.output / f"request-{request:08x}-{va+at:x}.bin"
                path.write_bytes(segment[start:end])
                result = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                                         f"--adjust-vma={va+start:#x}", str(path)],
                                        check=True, capture_output=True, text=True)
                path.with_suffix(".disasm").write_text(result.stdout)
                hits.append(dict(request=hex(request), candidate_address=hex(va+at), path=str(path)))
    (args.output / "candidates.json").write_text(json.dumps(hits, indent=2) + "\n")
    flow_hits = []
    for va, segment in executable_segments:
        candidates = []
        if args.call_target:
            for match in re.finditer(rb"[\xe8\xe9]", segment):
                at = match.start()
                if at + 5 > len(segment):
                    continue
                target = va + at + 5 + struct.unpack_from("<i", segment, at + 1)[0]
                if target in args.call_target:
                    candidates.append((at, f"flow-{target:x}", dict(target=hex(target))))
        if args.hci:
            for opcode in [0x200B, 0x200C, 0x200D, 0x200E, 0x2041, 0x2042, 0x2043]:
                pattern = b"\xbe" + struct.pack("<I", opcode)
                position = 0
                while True:
                    at = segment.find(pattern, position)
                    if at < 0:
                        break
                    position = at + 1
                    candidates.append((at, f"hci-{opcode:04x}", dict(opcode=hex(opcode))))
        for at, label, metadata in candidates:
            start, end = max(0, at - 128), min(len(segment), at + 512)
            path = args.output / f"{label}-{va+at:x}.bin"
            path.write_bytes(segment[start:end])
            result = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                                     f"--adjust-vma={va+start:#x}", str(path)],
                                    check=True, capture_output=True, text=True)
            path.with_suffix(".disasm").write_text(result.stdout)
            flow_hits.append(dict(candidate_address=hex(va+at), path=str(path), **metadata))
    if args.call_target or args.hci:
        (args.output / "flow-candidates.json").write_text(json.dumps(flow_hits, indent=2) + "\n")
        for item in flow_hits:
            print(item["candidate_address"], item.get("target", item.get("opcode")))
    if args.strings:
        (args.output / "bluetooth-strings.json").write_text(json.dumps(strings, indent=2) + "\n")
        print(f"{len(strings)} Bluetooth-related strings inventoried")
        wanted = {int(item["address"], 16): item["text"] for item in strings
                  if item["text"] in {"bt_gatt", "bt_dev", "bluetooth_hid", "bt_system_resume_phase1",
                                      "bt_system_suspend_phase3", "bt_process_resume"}}
        references = []
        for va, segment in executable_segments:
            for match in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", segment):
                at = match.start()
                if at + 7 > len(segment):
                    continue
                target = va + at + 7 + struct.unpack_from("<i", segment, at + 3)[0]
                if target not in wanted:
                    continue
                start, end = max(0, at - 128), min(len(segment), at + 1024)
                path = args.output / f"xref-{wanted[target]}-{va+at:x}.bin"
                path.write_bytes(segment[start:end])
                result = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                                         f"--adjust-vma={va+start:#x}", str(path)],
                                        check=True, capture_output=True, text=True)
                path.with_suffix(".disasm").write_text(result.stdout)
                references.append(dict(name=wanted[target], instruction=hex(va+at), path=str(path)))
        (args.output / "string-references.json").write_text(json.dumps(references, indent=2) + "\n")
        print(f"{len(references)} candidate RIP-relative string references")
    for hit in hits:
        print(hit["request"], hit["candidate_address"])
    print(f"{len(hits)} byte-match candidates; manual validation required")

if __name__ == "__main__":
    main()
