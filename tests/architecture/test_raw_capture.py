import io
import contextlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import nn2ctl
import nn2_raw_capture as raw


def packet(first=0, count=1, flags=1):
    prefix = struct.pack('<BBHIHHHH', 1, 3, 1, first, count, flags, 1, 0)
    sample = b''.join(x.to_bytes(3, 'little', signed=True) for x in (-8388608, -1, 0, 8388607)) + bytes((4, 0))
    return nn2ctl.encode_envelope(prefix + sample * count, msg_type=nn2ctl.FRAME, session_id=7)


class RawCaptureTests(unittest.TestCase):
    def test_allocation_rejection_has_clear_message(self):
        payload = struct.pack('<IHHBHBQQ', 1, nn2ctl.COMMANDS['set-mode'], 7, 1, 0, 0, 42, 100)
        reply = nn2ctl.describe_reply(nn2ctl.decode(nn2ctl.encode_envelope(payload, msg_type=nn2ctl.REPLY)))
        self.assertEqual(reply['state'], 'STOPPED')
        self.assertEqual(reply['message'], 'RAW buffer: not enough internal memory')
        with self.assertRaisesRegex(RuntimeError, 'RAW buffer: not enough internal memory'):
            nn2ctl.require_ok(reply)

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
        self.assertEqual((capture.breaks, capture.lost_in_breaks), (1, 8))
        capture.session = 8
        capture.accept(packet())
        self.assertEqual(capture.invalid, 1)
        self.assertEqual(struct.unpack_from('<H', binary.getvalue())[0], len(packet()))
        self.assertEqual(len(csv.getvalue().splitlines()), 4)

    def run_main_stop(self, error, state):
        class Clock:
            now = 0.0
            def monotonic(self):
                return self.now
            def sleep(self, seconds):
                self.now += seconds
        clock = Clock()

        class Socket:
            def settimeout(self, value):
                pass
            def recv(self, size):
                clock.now += 1.0
                return packet(count=40)

        class Client:
            session_id = 0
            stopped = closed = False
            sock = Socket()
            def request(self, command, payload=None):
                reply = dict(error='OK', state='STREAMING', fault=0, fault_name='NONE')
                if command == 'info':
                    reply['info'] = ';mode=ENGINEERING'
                elif command == 'start':
                    self.session_id = 7
                elif command == 'stop':
                    self.stopped = True
                    self.session_id = 0
                    reply['state'] = 'STOPPING'
                elif command == 'heartbeat' and self.stopped:
                    if isinstance(error, Exception):
                        raise error
                    reply.update(error=error, state=state)
                return reply
            def close(self):
                self.closed = True
        client = Client()
        output = io.StringIO()
        with tempfile.TemporaryDirectory() as directory:
            args = ['nn2_raw_capture.py', '--seconds', '1', '--output', str(Path(directory)/'raw')]
            with mock.patch.object(sys, 'argv', args), mock.patch.object(raw.nn2ctl, 'Client', return_value=client), \
                 mock.patch.object(raw.time, 'monotonic', clock.monotonic), \
                 mock.patch.object(raw.time, 'sleep', clock.sleep), \
                 contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                code = raw.main()
            self.assertTrue(client.closed)
            self.assertGreater((Path(directory)/'raw.bin').stat().st_size, 0)
            self.assertEqual(len((Path(directory)/'raw.csv').read_text().splitlines()), 41)
        return code, output.getvalue(), clock.now

    def test_released_session_stopped_heartbeat_confirms_stop(self):
        code, output, _ = self.run_main_stop('INVALID_STATE', 'STOPPED')
        self.assertEqual(code, 0)
        self.assertEqual(output.count('ENGINEERING only:'), 1)
        self.assertIn('packets=1 samples=40 gap=0 breaks=0 lost_in_breaks=0 invalid=0', output)
        self.assertNotIn('ERROR:', output)

    def test_ok_stopped_also_confirms_stop(self):
        code, output, _ = self.run_main_stop('OK', 'STOPPED')
        self.assertEqual(code, 0)
        self.assertIn('samples=40', output)
        self.assertNotIn('ERROR:', output)

    def test_stop_timeout_keeps_summary_before_error(self):
        code, output, now = self.run_main_stop('OK', 'STOPPING')
        self.assertNotEqual(code, 0)
        self.assertGreaterEqual(now, 6.0)  # One second capture plus five-second confirmation.
        self.assertIn('STOP confirmation timed out', output)
        self.assertLess(output.index('packets=1 samples=40'), output.index('ERROR:'))
        self.assertEqual(output.count('ENGINEERING only:'), 1)

    def test_missing_confirmation_replies_time_out_with_summary(self):
        code, output, now = self.run_main_stop(TimeoutError('no heartbeat reply'), 'STOPPING')
        self.assertNotEqual(code, 0)
        self.assertGreaterEqual(now, 6.0)
        self.assertIn('STOP confirmation timed out', output)
        self.assertLess(output.index('packets=1 samples=40'), output.index('ERROR:'))

    def test_invalid_state_without_stopped_remains_error(self):
        code, output, _ = self.run_main_stop('INVALID_STATE', 'STREAMING')
        self.assertNotEqual(code, 0)
        self.assertIn('INVALID_STATE', output)
        self.assertIn('state=STREAMING', output)
        self.assertLess(output.index('packets=1 samples=40'), output.index('ERROR:'))

    def test_udp_loss_and_firmware_gap_are_separate(self):
        capture = raw.Capture(io.BytesIO(), io.StringIO())
        capture.session = 7
        capture.accept(packet(first=0, count=40))
        capture.accept(packet(first=160, count=40))  # Three 40-sample packets missing.
        self.assertEqual((capture.gaps, capture.breaks, capture.lost_in_breaks), (0, 1, 120))
        capture.accept(packet(first=200, count=40, flags=3))
        self.assertEqual((capture.gaps, capture.breaks, capture.lost_in_breaks), (1, 1, 120))

    def test_duplicate_and_backward_breaks_do_not_invent_huge_loss(self):
        capture = raw.Capture(io.BytesIO(), io.StringIO())
        capture.session = 7
        for first in (10, 10, 9):
            capture.accept(packet(first=first))
        self.assertEqual((capture.breaks, capture.lost_in_breaks), (2, 0))

    def test_bench_sized_trace_has_35_breaks_and_2880_lost_samples(self):
        class Sink:
            def write(self, data):
                return len(data)
        class Writer:
            def writerow(self, row):
                pass
            def writerows(self, rows):
                pass
        # Exercise actual packet validation/counting without retaining a full bench file.
        with mock.patch.object(raw.csv, 'writer', return_value=Writer()):
            capture = raw.Capture(Sink(), io.StringIO())
        capture.session = 7
        first = 0
        gaps = [80]*34 + [160]
        blocks = [1]*35 + [474249]  # Received 474284; span 477164 including holes.
        for i, count in enumerate(blocks):
            while count:
                n = min(count, 100)
                capture.accept(packet(first=first, count=n))
                first += n
                count -= n
            if i < len(gaps):
                first += gaps[i]
        self.assertEqual(first, 477164)
        self.assertEqual(capture.samples, 474284)
        self.assertEqual((capture.gaps, capture.breaks, capture.lost_in_breaks), (0, 35, 2880))
        self.assertEqual(capture.invalid, 0)

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
