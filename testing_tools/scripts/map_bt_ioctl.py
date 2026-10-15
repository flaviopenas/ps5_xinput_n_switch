"""Map the offline kernel's manually identified native Bluetooth ioctl tables.

These locations belong only to the supplied image, not to firmware 4.03.
Output describes dispatch branches, not safe request contracts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

TABLES = [(0xffffffff81981c44, 0x80046209, 36),
          (0xffffffff81981cd4, 0x80086202, 51),
          (0xffffffff81981da0, 0x80106208, 47),
          (0xffffffff81981e5c, 0x80186201, 36),
          (0xffffffff81981eec, 0x80206216, 30)]

# Exact comparison branches manually verified in this image's dispatcher.
# Keep these separate from the arithmetic-indexed jump tables.
DIRECT = [(0x20046204, 0xffffffff80a6766a),
          (0x20046205, 0xffffffff80a67674),
          (0x20046226, 0xffffffff80a67452),
          (0x8004622a, 0xffffffff80a6724d),
          (0xc0106206, 0xffffffff80a6729b),
          (0xc0106237, 0xffffffff80a67478)]
IMAGE_SHA256 = "1e2e55bbd67ae52311f742b01440ec180493286b98ab2910404b2d0fa717d17f"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", type=Path, default=Path("testing_tools/build/native-requests/ioctl-map.json"))
    args = parser.parse_args()
    data = args.elf.read_bytes()
    if hashlib.sha256(data).hexdigest() != IMAGE_SHA256:
        parser.error("This map is specific to the previously inspected kernel image")
    if data[:6] != b"\x7fELF\x02\x01":
        parser.error("Expected little-endian ELF64")
    phoff = struct.unpack_from("<Q", data, 32)[0]
    stride, count = struct.unpack_from("<HH", data, 54)
    if stride != 56 or phoff + stride * count > len(data):
        parser.error("Invalid program headers")
    segments = []
    for i in range(count):
        kind, _, offset, va, _, size, _, _ = struct.unpack_from("<IIQQQQQQ", data, phoff + i * stride)
        if kind == 1:
            if offset + size > len(data):
                parser.error("Truncated segment")
            segments.append((offset, va, size))
    def read(va, size):
        for offset, base, length in segments:
            if 0 <= va-base and va-base+size <= length:
                return data[offset+va-base:offset+va-base+size]
        raise ValueError(f"Range not file-backed: {va:#x}")
    start = 0xffffffff80a67220
    binary = args.output.with_suffix(".bin")
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_bytes(read(start, 0x600))
    result = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                             f"--adjust-vma={start:#x}", str(binary)],
                            check=True, capture_output=True, text=True)
    binary.with_suffix(".disasm").write_text(result.stdout)
    instructions = []
    for line in result.stdout.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+)+\s*(\S.*)", line)
        if match:
            instructions.append((int(match[1], 16), match[2]))
    branches = [(request, target, "direct") for request, target in DIRECT]
    for table, base, entries in TABLES:
        raw = read(table, entries*4)
        for i in range(entries):
            target = table + struct.unpack_from("<i", raw, i*4)[0]
            if target == 0xffffffff80a672b5:
                continue
            branches.append((base+i, target, "table"))
    requests = []
    for request, target, source in sorted(branches):
        body = []
        for address, instruction in instructions:
            if address < target:
                continue
            if address >= target + 128:
                break
            body.append(instruction)
            if instruction.startswith("jmp") or instruction.startswith("ret"):
                break
        calls = [instruction.split()[-1] for instruction in body if instruction.startswith("call")]
        requests.append(dict(request=hex(request), target=hex(target), source=source,
                             calls=calls, instructions=body))
    args.output.write_text(json.dumps(dict(sha256=hashlib.sha256(data).hexdigest(),
                                         warning="Offline image only; request semantics unverified",
                                         requests=requests), indent=2) + "\n")
    for item in requests:
        print(item["request"], item["target"], ", ".join(item["calls"]))
    print(f"{len(requests)} request branches mapped; branch bodies can contain additional control flow")

if __name__ == "__main__":
    main()
