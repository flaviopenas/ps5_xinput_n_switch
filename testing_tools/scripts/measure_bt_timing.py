"""Capture bounded Bluetooth timing summaries from the running manager."""
import argparse
from pathlib import Path
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=9024)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--output", type=Path, default=Path("testing_tools/build/bt-timing-live.log"))
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("seconds must be positive")
    host = socket.gethostbyname(args.host)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    seen = set()
    summaries = 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client, args.output.open("w", encoding="utf-8") as output:
        client.bind(("0.0.0.0", 0))
        client.settimeout(0.5)
        end = time.monotonic() + args.seconds
        last_hello = 0
        print("Move sticks and tap buttons; capture is running.", flush=True)
        while time.monotonic() < end:
            if time.monotonic() - last_hello >= 1:
                client.sendto(b"PS5_XINPUT_LOG_HELLO\n", (host, args.port))
                last_hello = time.monotonic()
            try:
                packet, source = client.recvfrom(4096)
            except socket.timeout:
                continue
            if source != (host, args.port):
                continue
            for line in packet.decode("utf-8", errors="replace").splitlines():
                if "BT timing:" not in line or line in seen:
                    continue
                seen.add(line)
                summaries += 1
                output.write(line + "\n")
                output.flush()
                print(line, flush=True)
    print(f"Captured {summaries} distinct summaries to {args.output}. Historical log replay may be included.")
    if not summaries:
        print("No timing samples: check controller connection and manager build.")
    return 0 if summaries else 1


if __name__ == "__main__":
    raise SystemExit(main())
