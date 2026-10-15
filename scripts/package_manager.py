"""Package the main manager ELF, documentation and runtime license notices."""

import hashlib
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parent.parent
NOTICES = (
    "README.md", "CREDITS.md", "LICENSE", "third_party/README.md",
    "third_party/micro-ecc/LICENSE.txt", "third_party/tiny-aes/unlicense.txt",
)



def main():
    suffix = ""
    firmware = "4.03"
    binaries = ["ps5-xinput-manager.elf"]
    documents = sorted(ROOT.glob("docs/**/*.md"))
    files = [ROOT / name for name in NOTICES] + documents
    files += [ROOT / "dist" / name for name in binaries]
    for path in files:
        if not path.is_file():
            raise FileNotFoundError(f"missing bundle input: {path}")

    destination = ROOT / "dist" / f"ps5-xinput-manager{suffix}.zip"
    checksums = []
    with ZipFile(destination, "w", ZIP_DEFLATED) as archive:
        for path in files:
            # Preserve doc links; binaries live at the ZIP root.
            name = path.name if path.parent == ROOT / "dist" else path.relative_to(ROOT).as_posix()
            archive.write(path, name)
            if path.name in binaries and path.parent == ROOT / "dist":
                checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {name}\n")
        # Project hashes use dist/ paths; bundle hashes name extracted files.
        archive.writestr(f"SHA256SUMS.manager{suffix}", "".join(checksums))
        instructions = f"""# Controller Manager for firmware {firmware}

Load `{binaries[0]}`. The matching service is embedded; one manager ELF is enough.
Stop an older manager first: a launcher reuses a service already on port 9030.

The sender/log helpers are available in a source checkout under testing_tools.
From the project root, with an ELF loader running on the PS5:

```sh
python3 testing_tools/scripts/stop_payload.py PS5_IP --port 9024
python3 testing_tools/scripts/send_payload.py PS5_IP --elf dist/{binaries[0]}
```

Use the stop command only if an older service is running. Open the interface
at `http://PS5_IP:9030`. See [README.md](README.md) for support status and
[CREDITS.md](CREDITS.md) for attribution. Project build commands in the README
refer to a source checkout, not this binary bundle.

Verify extracted ELFs with `sha256sum -c SHA256SUMS.manager{suffix}`.
"""
        archive.writestr("BUNDLE.md", instructions)
    print(f"Packaged {destination.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
