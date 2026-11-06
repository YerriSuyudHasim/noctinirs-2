#!/usr/bin/env python3
"""ENGINEERING bench capture, never science. Owns START/heartbeat/STOP on one
UDP 3333 endpoint. Run set-mode --mode ENGINEERING while stopped first.
Binary output: repeated u16-LE datagram length followed by original NN2 bytes.
CSV output: sample_index, ain0..ain3, phase_class, slot_index.
Summary gap counts firmware GAP_BEFORE packets. breaks counts discontinuities
between consecutive accepted sample indices, including lost UDP packets;
lost_in_breaks sums forward missing positions across those breaks (u32 wrap
is contiguous). Duplicate/backward breaks add no missing positions. These are
between-sample observations, not a count of loss before/after the capture.
"""
import argparse
import csv
import math
import socket
import struct
import sys
import time
import nn2ctl


def decode_raw(wire):
    packet = nn2ctl.decode(wire)  # Includes envelope size and CRC-32 validation.
    p = packet.payload
    if packet.msg_type != nn2ctl.FRAME or len(p) < 16 or p[:2] != b'\x01\x03':
        raise ValueError('unsupported RAW layout/source')
    epoch, first, count, flags, geometry, reserved = struct.unpack_from('<HIHHHH', p, 2)
    if not 1 <= count <= 100 or len(p) != 16 + 14 * count or reserved or flags not in (1, 3):
        raise ValueError('RAW length/flags/reserved')
    rows = []
    for i in range(count):
        q = 16 + i * 14
        phase, slot = p[q+12:q+14]
        if phase > 8 or (phase == 0 and slot != 255) or (phase != 0 and slot >= 20):
            raise ValueError('RAW phase/slot')
        ain = [int.from_bytes(p[q+d*3:q+d*3+3], 'little', signed=True) for d in range(4)]
        rows.append(((first+i) & 0xffffffff, *ain, phase, slot))
    return packet, flags, rows


class Capture:
    def __init__(self, binary, csv_file):
        self.binary = binary
        self.csv = csv.writer(csv_file)
        self.csv.writerow(('sample_index', 'ain0', 'ain1', 'ain2', 'ain3', 'phase_class', 'slot_index'))
        self.packets = self.samples = self.gaps = self.invalid = 0
        self.breaks = self.lost_in_breaks = 0
        self.last = None
        self.session = None

    def accept(self, wire):
        try:
            packet, flags, rows = decode_raw(wire)
            if packet.session_id != self.session:
                raise ValueError('foreign session')
        except (ValueError, struct.error):
            self.invalid += 1
            return
        self.gaps += bool(flags & 2)
        if self.last is not None:
            missing = (rows[0][0] - self.last - 1) & 0xffffffff
            if missing:
                self.breaks += 1
                if missing < 0x80000000:
                    self.lost_in_breaks += missing
        self.last = rows[-1][0]
        self.binary.write(struct.pack('<H', len(wire)) + wire)
        self.csv.writerows(rows)
        self.packets += 1
        self.samples += len(rows)

    def summary(self):
        return (f'ENGINEERING only: packets={self.packets} samples={self.samples} '
                f'gap={self.gaps} breaks={self.breaks} lost_in_breaks={self.lost_in_breaks} '
                f'invalid={self.invalid}')


def confirm_stop(client):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            reply = client.request('heartbeat')
        except TimeoutError:
            time.sleep(0.05)
            continue
        # Session release is successful STOP evidence only in this exact state.
        if reply['error'] == 'INVALID_STATE' and reply['state'] == 'STOPPED':
            return
        nn2ctl.require_ok(reply)
        if reply['state'] == 'STOPPED':
            return
        time.sleep(0.05)
    raise RuntimeError('STOP confirmation timed out')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.4.1')
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--rate', type=int, choices=(10,), default=10)
    parser.add_argument('--output', default='nn2_raw')
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error('--seconds must be positive and finite')
    client = nn2ctl.Client(args.host, 3333)
    started = False
    try:
        info = nn2ctl.require_ok(client.request('info'))
        if ';mode=ENGINEERING' not in info['info']:
            raise RuntimeError('Set ENGINEERING while STOPPED first: nn2ctl.py set-mode --mode ENGINEERING')
        nn2ctl.require_ok(client.request('set-fs', bytes([args.rate])))
        with open(args.output+'.bin', 'wb') as binary, open(args.output+'.csv', 'w', newline='') as csv_file:
            capture = Capture(binary, csv_file)
            try:
                nn2ctl.require_ok(client.request('start'))
                started = True
                capture.session = client.session_id
                client.packet_sink = capture.accept
                end = time.monotonic() + args.seconds
                heartbeat = 0.0
                while time.monotonic() < end:
                    now = time.monotonic()
                    if now >= heartbeat:
                        reply = nn2ctl.require_ok(client.request('heartbeat'))
                        if reply['state'] not in ('STARTING', 'STREAMING') or client.session_id != capture.session:
                            raise RuntimeError('session ended during capture')
                        heartbeat = now + 0.5  # NN2_APP_HEARTBEAT_PERIOD_US = 500000.
                    client.sock.settimeout(min(0.05, max(0.001, end-time.monotonic())))
                    try:
                        wire = client.sock.recv(nn2ctl.MAX_PACKET+1)
                    except socket.timeout:
                        continue
                    capture.accept(wire)
                nn2ctl.require_ok(client.request('stop'))
                started = False
                confirm_stop(client)
            finally:
                try:
                    if started:
                        nn2ctl.require_ok(client.request('stop'))
                finally:
                    # Keep files open through cleanup so late accepted RAW is counted.
                    print(capture.summary(), flush=True)
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print('ERROR: '+str(error), file=sys.stderr, flush=True)
        return 1
    finally:
        client.close()


if __name__ == '__main__':
    raise SystemExit(main())
