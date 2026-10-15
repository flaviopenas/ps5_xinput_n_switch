"""Package matching manager/service ELFs, documentation and license notices."""

import argparse
import hashlib
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[2]
NOTICES = (
    "README.md", "CREDITS.md", "LICENSE", "third_party/README.md",
    "third_party/micro-ecc/LICENSE.txt", "third_party/tiny-aes/unlicense.txt",
    "testing_tools/licenses/Zephyr-LICENSE",
)
TOOLS = (
    "testing_tools/scripts/send_payload.py", "testing_tools/scripts/stop_payload.py",
    "testing_tools/scripts/watch_logs.py", "testing_tools/scripts/measure_bt_timing.py",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", choices=["403", "1360"], default="403")
    args = parser.parse_args()
    suffix = "-1360" if args.firmware == "1360" else ""
    firmware = "13.60" if suffix else "4.03"
    binaries = [f"ps5-xinput-{kind}{suffix}.elf" for kind in ("manager", "service")]
    documents = sorted(ROOT.glob("docs/**/*.md")) + sorted(ROOT.glob("testing_tools/docs/**/*.md"))
    files = [ROOT / name for name in (*NOTICES, *TOOLS)] + documents
    files += [ROOT / "testing_tools/dist" / name for name in binaries]
    for path in files:
        if not path.is_file():
            parser.error(f"missing bundle input: {path}")

    destination = ROOT / "testing_tools/dist" / f"ps5-xinput-manager{suffix}.zip"
    checksums = []
    with ZipFile(destination, "w", ZIP_DEFLATED) as archive:
        for path in files:
            # Preserve doc links; binaries live at the ZIP root.
            name = path.name if path.parent == ROOT / "testing_tools/dist" else path.relative_to(ROOT).as_posix()
            archive.write(path, name)
            if path.name in binaries and path.parent == ROOT / "testing_tools/dist":
                checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {name}\n")
        # Project hashes use dist/ paths; bundle hashes name extracted files.
        archive.writestr(f"SHA256SUMS.manager{suffix}", "".join(checksums))
        instructions = f"""# Controller Manager for firmware {firmware}

Load `{binaries[0]}`. The matching service is embedded; one manager ELF is enough.
Stop an older manager first: a launcher reuses a service already on port 9030.

From this extracted directory, with an ELF loader running on the PS5:

```sh
python3 testing_tools/scripts/stop_payload.py PS5_IP --port 9024
python3 testing_tools/scripts/send_payload.py PS5_IP --elf {binaries[0]}
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
