"""Inspect a decrypted ELF Pad module and extract selected virtual-pad exports."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from identify_bt_imports import nid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--output", type=Path, default=Path("testing_tools/build/pad-module"))
    args = parser.parse_args()
    data = args.module.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        parser.error("expected a readable ELF64 little-endian module")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    entsize, count = struct.unpack_from("<HH", data, 54)
    if entsize != 56 or phoff + count * entsize > len(data):
        parser.error("invalid ELF program headers")
    headers = [struct.unpack_from("<IIQQQQQQ", data, phoff+i*entsize) for i in range(count)]

    def at(address, size):
        for kind, flags, offset, vaddr, paddr, filesz, memsz, align in headers:
            if kind == 1 and vaddr <= address and address + size <= vaddr + filesz:
                start = offset + address - vaddr
                if start + size <= len(data):
                    return data[start:start+size]
        raise ValueError(f"unmapped file-backed range {address:#x}+{size:#x}")

    dynamic = next((h for h in headers if h[0] == 2), None)
    if not dynamic:
        parser.error("module has no dynamic table")
    tags = {}
    for offset in range(dynamic[2], dynamic[2]+dynamic[5], 16):
        tag, value = struct.unpack_from("<QQ", data, offset)
        if not tag:
            break
        tags[tag] = value
    if tags.get(11) != 24 or not all(k in tags for k in (5,6,10,0x6100003f)):
        parser.error("unsupported SCE dynamic symbol/string table")
    symbols = at(tags[6], tags[0x6100003f])
    strings = at(tags[5], tags[10])
    wanted = {nid(name): name for name in (
        "scePadVirtualDeviceAddDevice", "scePadVirtualDeviceInsertData", "scePadVirtualDeviceDeleteDevice")}
    args.output.mkdir(parents=True, exist_ok=True)
    metadata = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data), "exports": []}
    for offset in range(0, len(symbols), 24):
        name_offset, info, other, section, address, size = struct.unpack_from("<IBBHQQ", symbols, offset)
        end = strings.find(b"\0", name_offset)
        if end < 0:
            raise ValueError("unterminated symbol name")
        encoded = strings[name_offset:end].decode("ascii")
        name = wanted.get(encoded.split("#")[0])
        if not name or not address or not size:
            continue
        code = at(address, size)
        path = args.output / (name + ".bin")
        path.write_bytes(code)
        result = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                                 f"--adjust-vma={address:#x}", str(path)], check=True, capture_output=True, text=True)
        path.with_suffix(".disasm").write_text(result.stdout)
        metadata["exports"].append({"name": name, "address": hex(address), "size": size})
        print(f"{name}: {address:#x}, {size} bytes")
    if len(metadata["exports"]) != len(wanted):
        parser.error("not all three virtual-pad exports were found")
    (args.output / "module.json").write_text(json.dumps(metadata, indent=2)+"\n")
    print("Module SHA-256: " + metadata["sha256"])


if __name__ == "__main__":
    main()
