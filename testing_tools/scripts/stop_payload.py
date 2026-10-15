"""Request normal cleanup from a running PS5 XInput payload over UDP logs."""
import argparse
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="PS5 IP address")
    parser.add_argument("--port", type=int, default=9023,
                        help="payload UDP log port (default: 9023)")
    parser.add_argument("--local-port", type=int, default=19023,
                        help="local UDP port used by the log viewer (default: 19023)")
    parser.add_argument("--timeout", type=float, default=6.0,
                        help="seconds to wait for a live payload (default: 6)")
    args = parser.parse_args()
    address = (args.host, args.port)
    hello = b"PS5_XINPUT_LOG_HELLO\n"
    stop = b"PS5_XINPUT_STOP\n"
    ready = False
    acknowledged = False

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.bind(("0.0.0.0", args.local_port))
        client.settimeout(0.25)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline and not ready:
            client.sendto(hello, address)
            try:
                packet, _ = client.recvfrom(4096)
            except socket.timeout:
                continue
            message = packet.decode("utf-8", errors="replace").rstrip()
            if "Direct UDP log connection established" in message:
                ready = True
                print(f"Connected to the payload log stream at {args.host}:{args.port}.")
            elif "[ps5-xinput]" in message:
                print(message)

        if not ready:
            parser.exit(1, "No running payload log stream responded; nothing was stopped.\n")

        client.sendto(stop, address)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            try:
                packet, _ = client.recvfrom(4096)
            except socket.timeout:
                continue
            message = packet.decode("utf-8", errors="replace").rstrip()
            if message == "PS5_XINPUT_STOP_ACK":
                acknowledged = True
                print("Payload acknowledged the stop request; waiting for normal cleanup.")
            elif "[ps5-xinput]" in message:
                print(message)
            if ("Cleanup complete; payload exiting" in message or
                    "[ps5-xinput] Manager: service stopped" in message or
                    "[ps5-xinput] Reconnect observer: complete; own transport closed" in message):
                print("Payload exited cleanly.")
                return

        if not acknowledged:
            parser.exit(1, "No stop acknowledgement received.\n")
        print("Stop was acknowledged, but cleanup confirmation was not received before timeout.")


if __name__ == "__main__":
    main()
