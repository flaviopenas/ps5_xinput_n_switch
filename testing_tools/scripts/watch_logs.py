"""Show PS5 XInput UDP runtime logs and save them on the PC."""
import argparse
from collections import deque
from pathlib import Path
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9023)
    parser.add_argument("--remote-port", type=int, default=9023,
                        help="payload UDP port on the PS5")
    parser.add_argument("--bind", default="0.0.0.0", help="local listening address")
    parser.add_argument("--ps5", help="only accept logs from this PS5 IP or hostname")
    parser.add_argument("--output", type=Path, default=Path("ps5-xinput-live.log"))
    parser.add_argument("--deduplicate", action="store_true",
                        help="hide repeated lines replayed when another viewer connects")
    args = parser.parse_args()
    try:
        console_ip = socket.gethostbyname(args.ps5) if args.ps5 else None
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
            listener.bind((args.bind, args.port))
            listener.settimeout(0.5)
            with args.output.open("a", encoding="utf-8") as output:
                print(f"Listening on UDP {args.bind}:{args.port}; saving to {args.output}.",
                      flush=True)
                print("Start the payload on the PS5. Ctrl+C stops this viewer.", flush=True)
                if console_ip:
                    print(f"Requesting direct logs from {console_ip}:{args.remote_port}.", flush=True)
                last_request = 0.0
                recent = deque(maxlen=1024)
                seen = set()
                while True:
                    if console_ip and time.monotonic() - last_request >= 0.5:
                        listener.sendto(b"PS5_XINPUT_LOG_HELLO\n", (console_ip, args.remote_port))
                        last_request = time.monotonic()
                    try:
                        packet, source = listener.recvfrom(4096)
                    except socket.timeout:
                        continue
                    except ConnectionResetError:
                        # Windows can surface ICMP port-unreachable here while
                        # the console payload has not started listening yet.
                        continue
                    if console_ip and source[0] != console_ip:
                        continue
                    text = packet.decode("utf-8", errors="replace")
                    if "[ps5-xinput]" not in text:
                        continue
                    line = f"{source[0]} {text.rstrip()}\n"
                    if args.deduplicate:
                        if line in seen:
                            continue
                        if len(recent) == recent.maxlen:
                            seen.discard(recent[0])
                        recent.append(line)
                        seen.add(line)
                    print(line, end="", flush=True)
                    output.write(line)
                    output.flush()
    except KeyboardInterrupt:
        print("\nLog viewer stopped.")
    except OSError as exc:
        parser.exit(1, f"Log viewer failed: {exc}\n")


if __name__ == "__main__":
    main()
