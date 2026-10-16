#!/usr/bin/env python3
"""Capture and validate NoctiNIRS V4 stream-v1 UDP datagrams.

The tool binds UDP 3334, asks the device on UDP 3333 to stream to this host,
checks every stream-v1 CRC/sequence, and writes an optional bounded JSON-lines
capture. It does not start or stop acquisition; SCAN remains serial-owned.
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import time
from collections import Counter
from pathlib import Path


MAGIC = 0xA5
FORMAT_VERSION = 1
HEADER = struct.Struct("<BBBBHIH")
CRC = struct.Struct(">H")
FNIRS_FRAME_PREFIX = struct.Struct("<BBHIII")
V4_FRAME_PREFIX = struct.Struct("<6B2xIIQQI")
TYPE_NAMES = {3: "TIME64", 6: "FNIRS_CFG", 7: "FNIRS_FRAME",
              8: "SESSION", 11: "V4_OPTICAL_FRAME"}


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = (((crc << 1) ^ 0x1021) & 0xFFFF
                   if crc & 0x8000 else (crc << 1) & 0xFFFF)
    return crc


def parse_record(data: bytes) -> dict[str, int | str | bool]:
    if len(data) < HEADER.size + CRC.size:
        raise ValueError(f"short datagram length={len(data)}")
    magic, version, record_type, flags, sequence, relative_us, payload_len = (
        HEADER.unpack_from(data)
    )
    if magic != MAGIC or version != FORMAT_VERSION:
        raise ValueError(f"bad envelope magic=0x{magic:02X} version={version}")
    expected = HEADER.size + payload_len + CRC.size
    if len(data) != expected:
        raise ValueError(f"length={len(data)} expected={expected}")
    received_crc = CRC.unpack_from(data, HEADER.size + payload_len)[0]
    calculated_crc = crc16_ccitt_false(data[:-CRC.size])
    payload = data[HEADER.size:-CRC.size]
    result: dict[str, int | str | bool] = {
        "type": record_type,
        "type_name": TYPE_NAMES.get(record_type, f"UNKNOWN_{record_type}"),
        "flags": flags,
        "sequence": sequence,
        "relative_us": relative_us,
        "payload_len": payload_len,
        "crc_valid": received_crc == calculated_crc,
    }
    if record_type == 7 and payload_len == 356:
        payload_version, frame_flags, frame_id, valid_mask, underk_mask, _ = (
            FNIRS_FRAME_PREFIX.unpack_from(payload)
        )
        result.update(payload_version=payload_version, frame_id=frame_id,
                      phase="OFF" if frame_flags & 0x02 else "ON",
                      valid_mask=f"0x{valid_mask:05X}",
                      underk_mask=f"0x{underk_mask:05X}")
    elif record_type == 11 and payload_len >= V4_FRAME_PREFIX.size:
        (payload_version, frame_quality, target_slots, minimum_slots,
         invalid_slots, slot_count, session, frame_sequence, nominal_us,
         completed_us, frame_reason) = V4_FRAME_PREFIX.unpack_from(payload)
        result.update(payload_version=payload_version, session=session,
                      frame_sequence=frame_sequence, frame_quality=frame_quality,
                      target_slots=target_slots, minimum_slots=minimum_slots,
                      invalid_slots=invalid_slots, slot_count=slot_count,
                      nominal_us=nominal_us, completed_us=completed_us,
                      frame_reason=f"0x{frame_reason:08X}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="192.168.4.1")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--stream-port", type=int, default=3334)
    parser.add_argument("--control-port", type=int, default=3333)
    parser.add_argument("--duration", type=float, default=135.0)
    parser.add_argument("--compat", action="store_true",
                        help="request V3 paired ON/OFF records in addition to the V4 frame")
    parser.add_argument("--output", type=Path,
                        help="optional JSON-lines record metadata capture")
    args = parser.parse_args()

    counts: Counter[str] = Counter()
    sequence_gaps = 0
    previous_sequence: int | None = None
    output = args.output.open("w", encoding="utf-8") if args.output else None
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.stream_port))
    sock.settimeout(0.5)
    device = (args.device, args.control_port)
    sock.sendto(b"HELLO", device)
    sock.sendto(b"STREAM_START_COMPAT" if args.compat else b"STREAM_START", device)
    started = time.monotonic()
    print(f"capture_started bind={args.bind}:{args.stream_port} "
          f"device={args.device}:{args.control_port} duration_s={args.duration} "
          f"profile={'V3_PAIRED_PLUS_V4' if args.compat else 'V4_ONLY'}")
    try:
        while time.monotonic() - started < args.duration:
            try:
                data, source = sock.recvfrom(2048)
            except socket.timeout:
                continue
            if not data or data[0] != MAGIC:
                counts["CONTROL_REPLY"] += 1
                print(data.decode("ascii", errors="replace").strip())
                continue
            try:
                record = parse_record(data)
            except ValueError as exc:
                counts["MALFORMED"] += 1
                print(f"malformed source={source[0]}:{source[1]} error={exc}")
                continue
            counts[str(record["type_name"])] += 1
            if not record["crc_valid"]:
                counts["CRC_BAD"] += 1
            sequence = int(record["sequence"])
            if record["type"] == 8 and sequence == 0:
                previous_sequence = None
            if previous_sequence is not None and sequence != ((previous_sequence + 1) & 0xFFFF):
                sequence_gaps += (sequence - previous_sequence - 1) & 0xFFFF
            previous_sequence = sequence
            if output:
                output.write(json.dumps(record, separators=(",", ":")) + "\n")
    except KeyboardInterrupt:
        print("capture_interrupted")
    finally:
        sock.sendto(b"STREAM_STOP", device)
        if output:
            output.close()
        sock.close()

    print("capture_summary " + " ".join(
        f"{name}={count}" for name, count in sorted(counts.items())))
    print(f"capture_integrity sequence_gaps={sequence_gaps} "
          f"crc_bad={counts['CRC_BAD']} malformed={counts['MALFORMED']}")
    paired = min(counts["FNIRS_FRAME"] // 2, counts["V4_OPTICAL_FRAME"])
    print(f"capture_frames legacy_records={counts['FNIRS_FRAME']} "
          f"v4_quality_records={counts['V4_OPTICAL_FRAME']} paired_estimate={paired}")
    return 0 if not sequence_gaps and not counts["CRC_BAD"] and not counts["MALFORMED"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
