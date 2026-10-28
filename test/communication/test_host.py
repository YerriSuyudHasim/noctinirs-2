import argparse
import binascii
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import unittest
import zlib
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import nn2ctl

class CodecTests(unittest.TestCase):
    def test_vectors(self):
        for v in json.loads(Path(__file__).with_name('golden_vectors.json').read_text()):
            with self.subTest(name=v['name']):
                packet=nn2ctl.encode(v['id'],v['opcode'],bytes.fromhex(v['args_hex']),session_id=v['session'],sequence=v['sequence'])
                self.assertEqual(packet.hex(),v['wire_hex'])
                self.assertEqual(nn2ctl.decode(packet).payload,struct.pack('<IH',v['id'],v['opcode'])+bytes.fromhex(v['args_hex']))
        self.assertEqual(zlib.crc32(b'123456789'),0xcbf43926)
    def test_malformed(self):
        good=nn2ctl.encode(0,2)
        for n in range(len(good)):
            with self.assertRaises(ValueError): nn2ctl.decode(good[:n])
        for index in (0,4,5,6,8,10,len(good)-1):
            bad=bytearray(good);bad[index]^=0x80
            with self.assertRaises(ValueError):nn2ctl.decode(bad)
        with self.assertRaises(ValueError):nn2ctl.decode(good+b'\0')
        with self.assertRaises(ValueError):nn2ctl.encode(1,2,b'x'*1435)
        with self.assertRaises(ValueError):nn2ctl.decode(b'START\n')
    def test_sequence(self):
        t=nn2ctl.SequenceTracker()
        for v in (0xfffffffe,0xffffffff,0,0,3,2,4):t.observe(v)
        self.assertEqual((t.last,t.lost,t.duplicates,t.out_of_order),(4,2,1,1))
    def test_maximum_frame(self):
        frame=nn2ctl.encode_envelope(b'x'*1440,msg_type=nn2ctl.FRAME,flags=1)
        self.assertEqual(len(frame),1472)
        self.assertEqual(nn2ctl.decode(frame).payload,b'x'*1440)
    def test_reply_validation(self):
        payload=struct.pack('<IHHBHBQQ',1,0x10,0,3,0,1,42,100)
        packet=nn2ctl.decode(nn2ctl.encode_envelope(payload,msg_type=1,flags=1,session_id=7))
        self.assertEqual(nn2ctl.describe_reply(packet,1,0x10)['state'],'STREAMING')
        with self.assertRaises(ValueError):nn2ctl.describe_reply(packet,2,0x10)
        with self.assertRaises(ValueError):nn2ctl.describe_reply(packet,1,0x11)
    def test_legacy_helper(self):
        self.assertEqual(nn2ctl.provision_line('test','abcdefgh'),b'WIFI 74657374 6162636465666768\n')

class LoopbackTests(unittest.TestCase):
    executable=None
    def setUp(self):
        if not self.executable:self.skipTest('C loopback executable not supplied')
        self.process=subprocess.Popen([str(self.executable),'0'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        self.addCleanup(self.stop)
        line=self.process.stdout.readline().strip()
        self.assertTrue(line.isdigit(),line)
        self.client=nn2ctl.Client('127.0.0.1',int(line),timeout=0.2,retries=0)
        self.addCleanup(self.client.close)
    def stop(self):
        self.process.terminate();self.process.communicate(timeout=5)
    def test_control(self):
        c=self.client
        self.assertIn('ADR0004/1',c.request('info')['info'])
        self.assertEqual(c.request('set-fs',b'\x05')['error'],'OK')
        self.assertEqual(c.request('get-config')['rate_hz'],5)
        self.assertEqual(c.request('start')['state'],'STREAMING')
        session=c.session_id;self.assertNotEqual(session,0)
        self.assertEqual(c.request('start')['error'],'INVALID_STATE')
        self.assertEqual(c.request('heartbeat')['session_id'],session)
        self.assertGreaterEqual(c.request('get-time')['prepared_tx_us'],0)
        self.assertEqual(c.request('stop')['state'],'STOPPED')
        self.assertEqual(c.session_id,0)
        self.assertEqual(c.request('safe-off')['state'],'SAFE_OFF')
        self.assertEqual(c.request('start')['error'],'INVALID_STATE')
        self.assertEqual(c.request('clear-fault')['state'],'STOPPED')
    def test_duplicate_conflict_and_peer(self):
        c=self.client;c.sock.settimeout(.2)
        request=nn2ctl.encode(100,0x10)
        c.sock.send(request);first=c.sock.recv(2048)
        c.sock.send(request);self.assertEqual(c.sock.recv(2048),first)
        c.sock.send(nn2ctl.encode(100,0x11))
        self.assertEqual(nn2ctl.describe_reply(nn2ctl.decode(c.sock.recv(2048)))['error'],'INVALID_ARG')
        other=nn2ctl.Client('127.0.0.1',c.sock.getpeername()[1],timeout=.2,retries=0)
        self.addCleanup(other.close)
        self.assertEqual(other.request('stop')['error'],'INVALID_STATE')
        self.assertEqual(other.request('info')['error'],'OK')
        self.assertEqual(other.request('safe-off')['state'],'SAFE_OFF')
    def test_corruption_and_truncation(self):
        c=self.client;c.sock.settimeout(.15)
        request=nn2ctl.encode(1,2)
        for bad in (request[:-1]+bytes([request[-1]^1]),b'x'*1473,b'START\n'):
            c.sock.send(bad)
            with self.assertRaises(socket.timeout):c.sock.recv(2048)
        self.assertEqual(c.request('status')['state'],'STOPPED')
    def test_session_rejection(self):
        c=self.client;c.request('start');old=c.session_id;c.request('stop');c.request('start')
        self.assertNotEqual(old,c.session_id)
        c.sock.send(nn2ctl.encode(c.next_id,0x11,session_id=old))
        packet=nn2ctl.decode(c.sock.recv(2048))
        self.assertEqual(nn2ctl.describe_reply(packet)['error'],'INVALID_STATE')
        self.assertEqual(c.request('status')['state'],'STREAMING')
    def test_stop_ack_replay_after_end(self):
        c=self.client;c.request('start')
        packet=nn2ctl.encode(c.next_id,0x11,session_id=c.session_id)
        c.sock.settimeout(.2)
        c.sock.send(packet);first=c.sock.recv(2048)
        self.assertEqual(nn2ctl.describe_reply(nn2ctl.decode(first))['error'],'OK')
        c.sock.send(packet)
        self.assertEqual(c.sock.recv(2048),first)
    def test_expired_session_requires_explicit_restart(self):
        c=self.client;c.request('start');old=c.session_id
        time.sleep(4.6)  # 1.5s silence + 3s grace; this is native host evidence.
        self.assertEqual(c.request('status')['error'],'INVALID_STATE')
        self.assertEqual(c.session_id,0)
        self.assertEqual(c.request('status')['state'],'STOPPED')
        self.assertEqual(c.request('start')['state'],'STREAMING')
        self.assertNotEqual(c.session_id,old)
    def test_valid_crc_reserved_flags_and_protocol(self):
        c=self.client;c.sock.settimeout(.2)
        for offset,value,expected in ((10,4,'RESERVED_FLAGS'),(6,2,'BAD_FRAME')):
            packet=bytearray(nn2ctl.encode(90,0x10))
            struct.pack_into('<H',packet,offset,value)
            struct.pack_into('<I',packet,len(packet)-4,zlib.crc32(packet[:-4]))
            c.sock.send(packet)
            reply=nn2ctl.describe_reply(nn2ctl.decode(c.sock.recv(2048)))
            self.assertEqual(reply['error'],expected)
            self.assertEqual(reply['session_id'],0)
        self.assertEqual(c.request('status')['state'],'STOPPED')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--loopback',type=Path);args,rest=p.parse_known_args()
    if args.loopback:LoopbackTests.executable=args.loopback.resolve(strict=True)
    unittest.main(argv=[sys.argv[0]]+rest)
