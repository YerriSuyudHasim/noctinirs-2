import io
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import nn2ctl
import nn2_raw_capture as raw


def packet(first=0, count=1, flags=1):
    prefix = struct.pack('<BBHIHHHH', 1, 3, 1, first, count, flags, 1, 0)
    sample = b''.join(x.to_bytes(3, 'little', signed=True) for x in (-8388608, -1, 0, 8388607)) + bytes((4, 0))
    return nn2ctl.encode_envelope(prefix + sample * count, msg_type=nn2ctl.FRAME, session_id=7)


class RawCaptureTests(unittest.TestCase):
    def test_signed_samples_and_limits(self):
        for count in (1, 100):
            wire = packet(count=count)
            p, flags, rows = raw.decode_raw(wire)
            self.assertEqual(len(wire), 48 + count*14)
            self.assertEqual(len(rows), count)
            self.assertEqual(rows[0], (0, -8388608, -1, 0, 8388607, 4, 0))
        for count in (0, 101):
            with self.assertRaises(ValueError):
                raw.decode_raw(packet(count=count))

    def test_corrupt_crc_length_flags_and_phase(self):
        wire = packet()
        for bad in (wire[:-1], wire+b'\0', wire[:-1]+bytes([wire[-1]^1])):
            with self.assertRaises(ValueError):
                raw.decode_raw(bad)
        for offset, value in ((0, 2), (1, 2), (10, 0), (10, 5), (14, 1), (28, 9), (29, 255)):
            payload = bytearray(nn2ctl.decode(wire).payload)
            payload[offset] = value
            with self.assertRaises(ValueError):
                raw.decode_raw(nn2ctl.encode_envelope(payload, msg_type=nn2ctl.FRAME))

    def test_files_gap_wrap_and_foreign_owner(self):
        binary, csv = io.BytesIO(), io.StringIO()
        capture = raw.Capture(binary, csv)
        capture.session = 7
        for first, flags in ((0xffffffff, 1), (0, 1), (9, 3)):
            capture.accept(packet(first=first, flags=flags))
        self.assertEqual((capture.packets, capture.samples, capture.gaps, capture.invalid), (3, 3, 1, 0))
        capture.session = 8
        capture.accept(packet())
        self.assertEqual(capture.invalid, 1)
        self.assertEqual(struct.unpack_from('<H', binary.getvalue())[0], len(packet()))
        self.assertEqual(len(csv.getvalue().splitlines()), 4)

    def test_raw_received_during_heartbeat_is_retained(self):
        client = nn2ctl.Client('127.0.0.1', retries=0)
        self.addCleanup(client.close)
        client.session_id = 7
        collected = []
        client.packet_sink = collected.append
        original = client.sock

        class Socket:
            def send(self, wire):
                req = nn2ctl.decode(wire)
                rid, op = struct.unpack_from('<IH', req.payload)
                body = struct.pack('<IHHBHBQQ', rid, op, 0, 3, 0, 0, 42, 100)
                body += req.payload[6:] + struct.pack('<QQ', 100, 101)
                self.pending = [packet(), nn2ctl.encode_envelope(body, msg_type=nn2ctl.REPLY, session_id=7)]
            def settimeout(self, value):
                pass
            def recv(self, size):
                return self.pending.pop(0)
            def close(self):
                original.close()
        client.sock = Socket()
        self.assertEqual(client.request('heartbeat')['error'], 'OK')
        self.assertEqual(collected, [packet()])
