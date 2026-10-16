#!/usr/bin/env python3
"""Send one or more NoctiNIRSv3 UDP control commands."""
from __future__ import annotations

import argparse
import socket
import sys
import time


def send_command(sock: socket.socket, host: str, port: int, command: str, timeout: float) -> str:
    sock.settimeout(timeout)
    sock.sendto(command.encode("ascii"), (host, port))
    data, _ = sock.recvfrom(16384)
    return data.decode("utf-8", errors="replace").rstrip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host", help="NoctiNIRS SoftAP IP, usually 192.168.4.1")
    parser.add_argument("commands", nargs="+", help="Quote commands containing spaces")
    parser.add_argument("--port", type=int, default=3333)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for repetition in range(args.repeat):
            for command in args.commands:
                try:
                    reply = send_command(sock, args.host, args.port, command, args.timeout)
                except (TimeoutError, socket.timeout) as exc:
                    print(f"TIMEOUT command={command!r}: {exc}", file=sys.stderr)
                    return 2
                print(f"> {command}\n{reply}")
                if args.interval > 0 and (repetition + 1 < args.repeat or command != args.commands[-1]):
                    time.sleep(args.interval)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
