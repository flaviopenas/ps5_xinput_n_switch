"""Match captured import NIDs against identifiers in local SDK sources."""
import argparse
import base64
import hashlib
from pathlib import Path
import re

SALT = bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")

def nid(name):
    digest = hashlib.sha1(name.encode() + SALT).digest()[:8][::-1]
    return base64.b64encode(digest, altchars=b"+-").decode().rstrip("=")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--sdk", type=Path, default=Path("testing_tools/.tools/sdk-source"))
    args = parser.parse_args()
    imports = re.findall(r"BT import: index (\d+) relative stub (0x\w+).* symbol ([^#\s]+)",
                         args.log.read_text())
    wanted = {item[2] for item in imports}
    matches = {}
    for path in args.sdk.rglob("*"):
        if not path.is_file() or path.suffix not in {".h", ".c", ".S"}:
            continue
        for name in set(re.findall(r"\b[A-Za-z_][A-Za-z_0-9]*\b", path.read_text(errors="replace"))):
            encoded = nid(name)
            if encoded in wanted:
                matches[encoded] = name
    for index, stub, encoded in imports:
        print(f"{index:>2} {stub}: {matches.get(encoded, 'unresolved')} ({encoded})")

if __name__ == "__main__":
    main()
