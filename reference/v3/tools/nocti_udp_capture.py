#!/usr/bin/env python3
"""Capture and validate NoctiNIRS stream-v1 UDP records."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import socket
import sys
import time

from nocti_protocol import ProtocolError, decode_payload, parse_record


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=3334)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--jsonl", help="Optional decoded JSON-lines output")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    counts: Counter[str] = Counter()
    phases: dict[tuple[int, int], set[str]] = defaultdict(set)
    active_session = 0
    sequence_last: int | None = None
    sequence_gaps = 0
    output = open(args.jsonl, "w", encoding="utf-8") if args.jsonl else None
    start = time.monotonic()
    deadline = start + args.duration

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        sock.bind((args.bind, args.port))
        sock.settimeout(0.5)
        while time.monotonic() < deadline:
            try:
                datagram, source = sock.recvfrom(2048)
            except socket.timeout:
                continue
            try:
                record = parse_record(datagram)
                decoded = decode_payload(record)
            except ProtocolError as exc:
                counts["decode_errors"] += 1
                if not args.quiet:
                    print(f"DECODE_ERROR from={source}: {exc}", file=sys.stderr)
                continue
            counts[record.type_name] += 1
            if not record.crc_valid:
                counts["crc_bad"] += 1
            if record.type_name == "SESSION":
                active_session = int(decoded["session_id"])
                sequence_last = record.sequence
            else:
                if sequence_last is not None and record.sequence != ((sequence_last + 1) & 0xFFFF):
                    sequence_gaps += 1
                sequence_last = record.sequence
            if record.type_name == "FNIRS_FRAME":
                key = (active_session, int(decoded["frame_id"]))
                phases[key].add(str(decoded["phase"]))
                if decoded["partial"]:
                    counts["partial_phase_records"] += 1
                if decoded["underk_mask"]:
                    counts["underk_phase_records"] += 1
            entry = {
                "source": source[0],
                "type": record.type_name,
                "sequence": record.sequence,
                "relative_timestamp_us": record.relative_timestamp_us,
                "crc_valid": record.crc_valid,
                "payload": decoded,
            }
            if output:
                output.write(json.dumps(entry, separators=(",", ":")) + "\n")
            if not args.quiet:
                compact = {k: v for k, v in decoded.items() if k not in {"avg", "n_used", "payload_hex"}}
                print(json.dumps({**entry, "payload": compact}, separators=(",", ":")))

    if output:
        output.close()
    complete_pairs = sum(1 for value in phases.values() if value == {"LED_ON", "I_LED_OFF"})
    incomplete_pairs = len(phases) - complete_pairs
    summary = {
        "duration_s": round(time.monotonic() - start, 3),
        "counts": dict(counts),
        "sequence_gaps": sequence_gaps,
        "paired_frames_complete": complete_pairs,
        "paired_frames_incomplete": incomplete_pairs,
    }
    print("SUMMARY " + json.dumps(summary, sort_keys=True))
    return 0 if counts["decode_errors"] == 0 and counts["crc_bad"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
