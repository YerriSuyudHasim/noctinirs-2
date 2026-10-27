#!/usr/bin/env python3
"""ADR-0004/1 SoftAP control client. No firmware build, flash or discovery.

This profile has SIMULATED control sessions and no acquisition FRAME producer.
CF1 and historical ASCII/stream-v1 packets are intentionally incompatible.
"""
import argparse
import dataclasses
import json
import secrets
import socket
import struct
import sys
import time
import zlib

COMMANDS = {"heartbeat": 1, "info": 2, "status": 3, "get-config": 4, "get-time": 5,
            "start": 0x10, "stop": 0x11, "safe-off": 0x20, "clear-fault": 0x21,
            "set-fs": 0x100, "set-led-current": 0x101}
ERRORS = ("OK", "INVALID_STATE", "INVALID_ARG", "UNKNOWN_COMMAND", "HW_FAULT", "BUSY",
          "BAD_FRAME", "PRECONDITION_FAILED", "RESERVED_FLAGS")
STATES = ("BOOT", "STOPPED", "STARTING", "STREAMING", "STOPPING", "RECOVERING", "FAULTED", "SAFE_OFF")
HEADER = struct.Struct("<4sBBHHHIIQ")
MAX_PACKET = 1472
MAX_PAYLOAD = 1440
REQUEST, REPLY, FRAME, SESSION_REPORT, EVENT = range(5)
SIMULATED, TS_ESTIMATED = 1, 2

@dataclasses.dataclass(frozen=True)
class Envelope:
    msg_type: int
    protocol_version: int
    flags: int
    session_id: int
    sequence: int
    timestamp_us: int
    payload: bytes

def encode_envelope(payload=b"", *, msg_type=REQUEST, protocol_version=1, flags=0,
                    session_id=0, sequence=0, timestamp_us=0):
    if msg_type not in range(5) or len(payload) > MAX_PAYLOAD or flags & ~3:
        raise ValueError("envelope type/flags/size")
    body = HEADER.pack(b"NNS2", 1, msg_type, protocol_version, len(payload), flags,
                       session_id, sequence, timestamp_us) + payload
    return body + struct.pack("<I", zlib.crc32(body))

def encode(request_id, opcode, payload=b"", *, session_id=0, sequence=0):
    if not 0 <= request_id <= 0xffffffff or not 1 <= opcode <= 0xffff:
        raise ValueError("request ID/opcode range")
    return encode_envelope(struct.pack("<IH", request_id, opcode) + payload,
                           session_id=session_id, sequence=sequence)

def decode(data):
    if not 32 <= len(data) <= MAX_PACKET:
        raise ValueError("packet size")
    magic, framing, kind, protocol, size, flags, session, sequence, stamp = HEADER.unpack_from(data)
    if magic != b"NNS2" or framing != 1 or kind not in range(5) or protocol != 1 or flags & ~3:
        raise ValueError("unsupported envelope")
    if size > MAX_PAYLOAD or len(data) != 32 + size:
        raise ValueError("payload length")
    if zlib.crc32(data[:-4]) != struct.unpack_from("<I", data, len(data)-4)[0]:
        raise ValueError("CRC32")
    return Envelope(kind, protocol, flags, session, sequence, stamp, data[28:-4])

def describe_reply(packet, request_id=None, opcode=None):
    if packet.msg_type != REPLY or len(packet.payload) < 28:
        raise ValueError("short/non-reply packet")
    rid, op, error = struct.unpack_from("<IHH", packet.payload)
    if request_id is not None and rid != request_id or opcode is not None and op != opcode:
        raise ValueError("transaction correlation")
    state, fault, simulated, boot, rx = struct.unpack_from("<BH B QQ", packet.payload, 8)
    if error >= len(ERRORS) or state >= len(STATES) or simulated not in (0,1):
        raise ValueError("reply fields")
    if bool(packet.flags & SIMULATED) != bool(simulated):
        raise ValueError("simulation provenance")
    extra = packet.payload[28:]
    result = dict(request_id=rid, opcode=op, error=ERRORS[error], state=STATES[state],
                  simulated=bool(simulated), fault=fault, boot_id=boot, device_us=packet.timestamp_us,
                  session_id=packet.session_id, msg_sequence=packet.sequence, received_us=rx)
    if error:
        if extra: raise ValueError("unexpected error extension")
    elif op == COMMANDS["info"]:
        result["info"] = extra.decode("ascii")
    elif op == COMMANDS["get-config"]:
        if len(extra) != 42 or extra[1] != 20: raise ValueError("config length")
        result.update(rate_hz=extra[0], slots=extra[1], current_ma=list(struct.unpack_from("<20H", extra, 2)))
    elif op in (1,3,5):
        if len(extra) != (45 if op == 3 else 24): raise ValueError("time/status length")
        echo, rx, tx = struct.unpack_from("<QQQ", extra)
        if tx < rx: raise ValueError("backward device timestamps")
        result.update(t_host=echo, received_us=rx, prepared_tx_us=tx)
        if op == 3:
            req, dup, expired, malformed, reserved, grace = struct.unpack_from("<IIIIIB", extra, 24)
            if grace > 1: raise ValueError("grace field")
            result.update(requests=req, duplicates=dup, lease_expirations=expired,
                          malformed=malformed, reserved_flags=reserved, grace_active=bool(grace))
    elif extra:
        raise ValueError("unexpected command data")
    return result

class SequenceTracker:
    """Track one sequence domain. Never mix message/frame/sample counters."""
    def __init__(self):
        self.last = None
        self.lost = self.duplicates = self.out_of_order = 0
    def observe(self, sequence):
        if not 0 <= sequence <= 0xffffffff: raise ValueError("sequence range")
        if self.last is None: self.last = sequence; return
        delta = (sequence-self.last) & 0xffffffff
        if delta == 0: self.duplicates += 1
        elif delta >= 0x80000000: self.out_of_order += 1
        else: self.lost += delta-1; self.last = sequence

class Client:
    def __init__(self, host, port=3333, timeout=0.5, retries=2):
        if timeout <= 0 or not 0 <= retries <= 10: raise ValueError("timeout/retries")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect((host,port))
        self.timeout, self.retries = timeout, retries
        self.next_id = secrets.randbelow(0x7fffffff)+1
        self.boot_id = None
        self.session_id = 0
        self.sequence = 0
    def close(self): self.sock.close()
    def request(self, command, payload=None):
        op = COMMANDS[command]
        if payload is None:
            payload = struct.pack("<Q", time.monotonic_ns()//1000) if op in (1,3,5) else b""
        rid = self.next_id
        self.next_id = (rid+1) & 0xffffffff
        self.sequence = (self.sequence+1) & 0xffffffff
        wire = encode(rid,op,payload,session_id=self.session_id,sequence=self.sequence)
        first_ns = time.monotonic_ns()
        expected_echo = struct.unpack("<Q",payload)[0] if op in (1,3,5) and len(payload)==8 else None
        for attempt in range(self.retries+1):
            self.sock.send(wire)
            deadline = time.monotonic()+self.timeout
            # A packet-count cap also bounds work under unrelated valid traffic.
            for _ in range(64):
                remaining = deadline-time.monotonic()
                if remaining <= 0: break
                self.sock.settimeout(remaining)
                try: received = self.sock.recv(MAX_PACKET+1)
                except socket.timeout: break
                try:
                    packet = decode(received)
                    result = describe_reply(packet,rid,op)
                    if result["error"] == "OK" and expected_echo is not None and result.get("t_host") != expected_echo:
                        continue
                    # Session-changing replies may return a new ID or zero. All
                    # other successful session replies must match the held ID.
                    if result["error"] == "OK" and self.session_id and op not in (0x10,0x11,0x20,0x21) and packet.session_id != self.session_id:
                        continue
                except (ValueError,UnicodeDecodeError,struct.error): continue
                if self.boot_id is not None and self.boot_id != result["boot_id"]:
                    self.session_id=0
                    raise RuntimeError("device reboot: reconnect with a new Client")
                self.boot_id=result["boot_id"]
                if result["error"]=="INVALID_STATE" and packet.session_id != self.session_id:
                    # Lost/expired session: forget the stale ID; never adopt a
                    # different owner's session or restart automatically.
                    self.session_id=0
                if result["error"]=="OK" and op in (0x10,0x11,0x20,0x21):
                    self.session_id=packet.session_id
                result.update(retries=attempt, round_trip_us=(time.monotonic_ns()-first_ns)//1000)
                return result
        raise TimeoutError(f"no matching {command} reply after {self.retries+1} attempts")

def require_ok(result):
    if result["error"] != "OK": raise RuntimeError("device returned "+result["error"])
    return result

def provision_line(ssid,password):
    # Legacy encoding helper only; SoftAP firmware rejects provisioning commands.
    s,p=ssid.encode("utf8"),password.encode("ascii")
    if not 1<=len(s)<=32 or b"\0" in s or not 8<=len(p)<=63 or any(c<32 or c>126 for c in p):
        raise ValueError("credential encoding")
    return b"WIFI "+s.hex().encode()+b" "+p.hex().encode()+b"\n"

def usb_status(serial_port):
    if not serial_port: raise ValueError('--serial-port is required')
    try: import serial
    except ImportError as error: raise RuntimeError('Use the existing ESP-IDF Python environment with pyserial') from error
    # Configure control lines while closed. Opening with pySerial's asserted
    # DTR/RTS defaults can trigger ESP32-S3 USB Serial/JTAG reset transitions.
    # Driver/OS glitches remain possible and require a same-boot bench check.
    port = serial.Serial(port=None, baudrate=115200, timeout=.2, write_timeout=1,
                         xonxoff=False, rtscts=False, dsrdtr=False)
    try:
        port.dtr = False
        port.rts = False
        port.port = serial_port
        port.open()
        port.reset_input_buffer();port.write(b'\n');time.sleep(.1);port.reset_input_buffer()
        port.write(b'STATUS\n');deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            line=port.readline(1024).decode('utf8',errors='replace').strip()
            if line.startswith('STATUS '): print(line);return
            if line.startswith('ERR '): raise RuntimeError(line)
        raise TimeoutError('no complete USB STATUS response')
    finally:
        port.close()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command",choices=list(COMMANDS)+["run", "usb-status"])
    parser.add_argument("--serial-port")
    parser.add_argument("--host",default="192.168.4.1")
    parser.add_argument("--port",type=int,default=3333)
    parser.add_argument("--timeout",type=float,default=0.5)
    parser.add_argument("--retries",type=int,default=2)
    parser.add_argument("--rate",type=int,default=None,
                        help="Frame rate for set-fs, or optional configuration before start/run; boot default is 5 Hz")
    parser.add_argument("--channel",type=int,default=0)
    parser.add_argument("--current-ma",type=int,default=0)
    parser.add_argument("--seconds",type=float,default=10)
    parser.add_argument("--led",action="store_true",
                        help="Explicitly start real LEDs in the opt-in bring-up profile; requires an approved bench procedure")
    args=parser.parse_args()
    client=None
    try:
        if args.command=="usb-status":
            usb_status(args.serial_port);return 0
        if not 1<=args.port<=65535: raise ValueError("UDP port")
        if args.command=="set-fs" and args.rate is None: args.rate=5
        if args.rate is not None and args.rate not in (1,2,4,5,8,10): raise ValueError("rate")
        if args.command=="set-led-current" and not (0<=args.channel<20 and 0<=args.current_ma<=50): raise ValueError("channel/current")
        if args.command=="run" and not 0<args.seconds<=86400: raise ValueError("duration")
        client=Client(args.host,args.port,args.timeout,args.retries)
        if args.command in ("start", "run"):
            info = require_ok(client.request("info"))
            real_led = "led=REAL_BRINGUP" in info["info"]
            if real_led and not args.led:
                raise RuntimeError("Real LED profile: START can energize emitters. Use --led only under the approved bench procedure")
            if args.led and not real_led:
                raise RuntimeError("Real LED bring-up is not enabled in this firmware")
            if args.rate is not None:
                require_ok(client.request("set-fs", bytes([args.rate])))
        if args.command=="run":
            print(json.dumps(require_ok(client.request("start"))),flush=True)
            try:
                deadline=time.monotonic()+args.seconds
                while time.monotonic()<deadline:
                    result = client.request("heartbeat")
                    print(json.dumps(result),flush=True)
                    require_ok(result)
                    if result["state"] not in ("STARTING", "STREAMING"):
                        raise RuntimeError("run ended in "+result["state"])
                    time.sleep(min(0.5,max(0,deadline-time.monotonic())))
            finally:
                result = client.request("stop")
                print(json.dumps(result),flush=True)
                # STOP can acknowledge a pending hardware drain. Poll until
                # complete; do not report success solely from its acceptance.
                deadline = time.monotonic()+2
                while result["error"] == "OK" and result["state"] == "STOPPING":
                    if time.monotonic() >= deadline: raise TimeoutError("LED stop did not complete")
                    time.sleep(.05)
                    result = client.request("status")
                    if result["error"] == "INVALID_STATE" and client.session_id == 0:
                        result = client.request("status")
                    print(json.dumps(result),flush=True)
                require_ok(result)
                if result["state"] != "STOPPED": raise RuntimeError("stop ended in "+result["state"])
        else:
            payload=bytes([args.rate]) if args.command=="set-fs" else (
                struct.pack("<BH",args.channel,args.current_ma) if args.command=="set-led-current" else None)
            print(json.dumps(require_ok(client.request(args.command,payload))))
        return 0
    except (OSError,RuntimeError,ValueError) as error:
        print("ERROR: "+str(error),file=sys.stderr);return 1
    finally:
        if client: client.close()
if __name__=="__main__": raise SystemExit(main())
