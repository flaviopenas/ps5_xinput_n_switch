"""Reassemble captured Pad or Bluetooth HID code for local disassembly."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", type=Path,
                        default=[Path("ps5-xinput-debug.log"), Path("ps5-xinput-live.log")])
    parser.add_argument("--output", type=Path, default=Path("testing_tools/build/pad-abi"))
    parser.add_argument("--prefix", choices=["pad", "bt"], default="pad")
    parser.add_argument("--disassemble", action="store_true", help="Write objdump disassembly beside each capture")
    parser.add_argument("--symbol", action="append", help="Extract only these symbols (repeatable)")
    args = parser.parse_args()
    label = "Pad ABI" if args.prefix == "pad" else "BT ABI"
    pattern = re.compile(re.escape(label) + r": (\w+) (0x[0-9A-Fa-f]+) \+([0-9A-Fa-f]+) ([0-9a-f]+)$")
    captures = {}
    for path in args.logs:
        if not path.exists():
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = pattern.search(line)
            if match:
                name, address, offset, hex_data = match.groups()
                if args.symbol and name not in args.symbol:
                    continue
                captures.setdefault((name, int(address, 16)), {})[int(offset, 16)] = bytes.fromhex(hex_data)
    args.output.mkdir(parents=True, exist_ok=True)
    metadata = []
    for (name, address), chunks in captures.items():
        data = bytearray()
        for offset, chunk in sorted(chunks.items()):
            if offset != len(data):
                parser.exit(1, f"Missing chunk in {name} at +{len(data):04X}; refusing incomplete capture.\n")
            data.extend(chunk)
        destination = args.output / f"{name}-{address:x}.bin"
        destination.write_bytes(data)
        if args.disassemble:
            result = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                                     f"--adjust-vma={address:#x}", str(destination)],
                                    check=True, capture_output=True, text=True)
            destination.with_suffix(".disasm").write_text(result.stdout, encoding="utf-8")
        metadata.append({"symbol": name, "address": hex(address), "bytes": len(data), "path": str(destination)})
        print(f"{name}: {len(data)} bytes at {address:#x} -> {destination}")
    (args.output / "captures.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    if not metadata:
        parser.exit(1, "No ABI chunks found.\n")


if __name__ == "__main__":
    main()
