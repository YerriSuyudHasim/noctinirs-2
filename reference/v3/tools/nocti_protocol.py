#!/usr/bin/env python3
"""NoctiNIRSv3/v16 stream-v1 protocol helpers.

The stream envelope and fNIRS payloads are intentionally compatible with
NoctiNIRSv2(16): little-endian packed header/payload and big-endian CRC bytes.
"""
from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Any

MAGIC = 0xA5
FORMAT_VERSION = 1
HEADER = struct.Struct("<BBBBHIH")
CRC = struct.Struct(">H")
SESSION_V1 = struct.Struct("<B3xIQ")
FNIRS_CFG_V2 = struct.Struct("<8B4H")
FNIRS_FRAME_PREFIX = struct.Struct("<BBHIII")
FNIRS_FRAME_AVG = struct.Struct("<80i")
FNIRS_FRAME_N_USED = struct.Struct("<20B")
FNIRS_FRAME_V2_SIZE = (
    FNIRS_FRAME_PREFIX.size + FNIRS_FRAME_AVG.size + FNIRS_FRAME_N_USED.size
)

REC_NAMES = {
    1: "STATUS",
    2: "EVENT",
    3: "TIME64",
    4: "FNIRS",
    5: "BIOPOT",
    6: "FNIRS_CFG",
    7: "FNIRS_FRAME",
    8: "SESSION",
    9: "BIOPOT_CFG",
    10: "BIOPOT_FRAME",
}

F_PARTIAL = 0x01
F_LED_OFF = 0x02
F_PAIRED = 0x04


class ProtocolError(ValueError):
    """Raised when a UDP datagram is not a valid stream-v1 record."""


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass(frozen=True)
class StreamRecord:
    record_type: int
    flags: int
    sequence: int
    relative_timestamp_us: int
    payload: bytes
    crc_received: int
    crc_valid: bool

    @property
    def type_name(self) -> str:
        return REC_NAMES.get(self.record_type, f"UNKNOWN_{self.record_type}")


def parse_record(datagram: bytes) -> StreamRecord:
    if len(datagram) < HEADER.size + CRC.size:
        raise ProtocolError(f"short datagram: {len(datagram)}")
    magic, version, rec_type, flags, sequence, rel_us, payload_len = HEADER.unpack_from(datagram)
    if magic != MAGIC:
        raise ProtocolError(f"bad magic: 0x{magic:02X}")
    if version != FORMAT_VERSION:
        raise ProtocolError(f"unsupported stream version: {version}")
    expected = HEADER.size + payload_len + CRC.size
    if len(datagram) != expected:
        raise ProtocolError(f"length mismatch: got={len(datagram)} expected={expected}")
    payload = datagram[HEADER.size:HEADER.size + payload_len]
    crc_received = CRC.unpack_from(datagram, HEADER.size + payload_len)[0]
    crc_calculated = crc16_ccitt_false(datagram[:-CRC.size])
    return StreamRecord(
        record_type=rec_type,
        flags=flags,
        sequence=sequence,
        relative_timestamp_us=rel_us,
        payload=payload,
        crc_received=crc_received,
        crc_valid=crc_received == crc_calculated,
    )


def decode_payload(record: StreamRecord) -> dict[str, Any]:
    payload = record.payload
    if record.record_type == 8:
        if len(payload) != SESSION_V1.size:
            raise ProtocolError(f"SESSION length={len(payload)}")
        version, session_id, t0_us = SESSION_V1.unpack(payload)
        return {"version": version, "session_id": session_id, "t0_us": t0_us}
    if record.record_type == 3:
        if len(payload) != 8:
            raise ProtocolError(f"TIME64 length={len(payload)}")
        return {"absolute_timestamp_us": int.from_bytes(payload, "big")}
    if record.record_type == 6:
        if len(payload) != FNIRS_CFG_V2.size:
            raise ProtocolError(f"FNIRS_CFG length={len(payload)}")
        values = FNIRS_CFG_V2.unpack(payload)
        keys = (
            "version", "map_mode", "frame_fs_hz", "slot_count", "k_on", "k_off",
            "current_735_mA", "current_850_mA", "on_settle_us", "on_window_us",
            "off_min_settle_us", "slot_period_us",
        )
        return dict(zip(keys, values, strict=True))
    if record.record_type == 7:
        if len(payload) != FNIRS_FRAME_V2_SIZE:
            raise ProtocolError(f"FNIRS_FRAME length={len(payload)}")
        version, flags, frame_id, valid_mask, underk_mask, crc_mask = FNIRS_FRAME_PREFIX.unpack_from(payload)
        offset = FNIRS_FRAME_PREFIX.size
        avg_flat = FNIRS_FRAME_AVG.unpack_from(payload, offset)
        offset += FNIRS_FRAME_AVG.size
        n_used = FNIRS_FRAME_N_USED.unpack_from(payload, offset)
        avg = [list(avg_flat[i * 4:(i + 1) * 4]) for i in range(20)]
        return {
            "version": version,
            "flags": flags,
            "frame_id": frame_id,
            "phase": "I_LED_OFF" if flags & F_LED_OFF else "LED_ON",
            "paired": bool(flags & F_PAIRED),
            "partial": bool(flags & F_PARTIAL),
            "valid_mask": valid_mask,
            "underk_mask": underk_mask,
            "crc_mask": crc_mask,
            "avg": avg,
            "n_used": list(n_used),
        }
    return {"payload_hex": payload.hex()}
