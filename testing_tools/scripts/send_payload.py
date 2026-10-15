"""Send a built ELF to an already running PS5 TCP ELF loader."""
import argparse
from pathlib import Path
import socket


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="PS5 IP address")
    parser.add_argument("--port", type=int, default=9021)
    parser.add_argument("--elf", type=Path,
                        default=Path(__file__).resolve().parents[2] / "dist/ps5-xinput.elf")
    args = parser.parse_args()
    data = args.elf.read_bytes()
    if data[:4] != b"\x7fELF":
        parser.error("input is not an ELF file")
    try:
        with socket.create_connection((args.host, args.port), timeout=10) as conn:
            conn.sendall(data)
            conn.shutdown(socket.SHUT_WR)
    except OSError as exc:
        parser.exit(1, f"Payload transfer failed: {exc}\n")
    print(f"Sent {len(data)} bytes to {args.host}:{args.port}. "
          "Transfer success does not confirm controller compatibility.")


if __name__ == "__main__":
    main()
