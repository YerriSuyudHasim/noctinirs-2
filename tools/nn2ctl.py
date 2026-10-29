#!/usr/bin/env python3
"""ADR-0004/1 SoftAP control client. No firmware build, flash or discovery.

This profile has SIMULATED control sessions and no acquisition FRAME producer.
CF1 and historical ASCII/stream-v1 packets are intentionally incompatible.
"""
import argparse
import dataclasses
import json
import math
import secrets
import socket
import struct
import sys
import time
import zlib

COMMANDS = {"heartbeat": 1, "info": 2, "status": 3, "get-config": 4, "get-time": 5,
            "start": 0x10, "stop": 0x11, "safe-off": 0x20, "clear-fault": 0x21,
            "health": 0x30, "timing": 0x31,
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
    elif op in (COMMANDS["health"], COMMANDS["timing"]):
        result["diag"] = parse_diag(extra.decode("ascii"))
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

def describe_frame(packet):
    """ACQ1 local four-detector raw-count aggregates. No analog current model."""
    p = packet.payload
    if packet.msg_type != FRAME or len(p) != 1156 or p[:4] != b"ACQ1":
        raise ValueError("unsupported acquisition frame")
    frame, adc_session, origin = struct.unpack_from("<IIQ", p, 4)
    slots, detectors, target, minimum = p[20:24]
    if (slots, detectors, target, minimum) != (20, 4, 10, 8) or p[30] not in (0, 1):
        raise ValueError("acquisition geometry")
    records = []
    for i in range(20):
        q = 36 + i*56
        slot, n_on, n_off, quality, reason = struct.unpack_from("<BBBBI", p, q)
        on_sum = list(struct.unpack_from("<4i", p, q+8))
        off_sum = list(struct.unpack_from("<4i", p, q+24))
        on = [x/n_on for x in on_sum] if n_on else None
        off = [x/n_off for x in off_sum] if n_off else None
        if slot != i or max(n_on, n_off) > target or quality > 4:
            raise ValueError("acquisition slot")
        valid = (quality in (0, 1) and minimum <= n_on == n_off <= target
                 and not p[30] and not reason and -2147483648 not in on_sum+off_sum)
        records.append(dict(slot=slot, source=slot//2+1,
            wavelength_nm=735 if slot%2==0 else 850, quality=quality, reason=reason,
            n_on=n_on, n_off=n_off, valid=valid, on_sum_counts=on_sum, off_sum_counts=off_sum, on_counts=on, off_counts=off,
            corrected_counts=[x-y for x,y in zip(on,off)] if valid else None,
            commit_offset_us=list(struct.unpack_from("<2H",p,q+40)),
            stable_us=list(struct.unpack_from("<2H",p,q+44)),
            first_adc_sequence=list(struct.unpack_from("<2I",p,q+48))))
    return dict(type="FRAME", schema="ACQ1", frame_sequence=frame,
        session_id=packet.session_id, adc_session=adc_session, origin_us=origin,
        message_sequence=packet.sequence, units="ADC counts", channels=["CH0/PD3","CH1/PD4","CH2/PD1","CH3/PD2"],
        timestamp_estimated=bool(packet.flags & TS_ESTIMATED), simulated=bool(packet.flags & SIMULATED),
        producer_dropped=struct.unpack_from("<I",p,32)[0], warmup=bool(p[30]), slots=records)


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
        self.frame_sink = None
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
                    if packet.msg_type == FRAME:
                        if self.session_id and packet.session_id == self.session_id and self.frame_sink:
                            self.frame_sink(describe_frame(packet))
                        continue
                    result = describe_reply(packet,rid,op)
                    if result["error"] == "OK" and expected_echo is not None and result.get("t_host") != expected_echo:
                        continue
                    # Session-changing replies may return a new ID or zero. All
                    # other successful session replies must match the held ID.
                    if result["error"] == "OK" and self.session_id and op not in (0x10,0x11,0x20,0x21,0x30,0x31) and packet.session_id != self.session_id:
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
    if result["error"] != "OK":
        raise RuntimeError("device returned "+result["error"]+
                           f" (state={result.get('state', 'unknown')}, fault={result.get('fault', 'unknown')})")
    return result

def provision_line(ssid,password):
    # Legacy encoding helper only; SoftAP firmware rejects provisioning commands.
    s,p=ssid.encode("utf8"),password.encode("ascii")
    if not 1<=len(s)<=32 or b"\0" in s or not 8<=len(p)<=63 or any(c<32 or c>126 for c in p):
        raise ValueError("credential encoding")
    return b"WIFI "+s.hex().encode()+b" "+p.hex().encode()+b"\n"

def parse_diag(text):
    """Parse a V4 health or timing report. This is not an ACQ1 frame."""
    if not isinstance(text, str) or not text.startswith(("v4health=1 ", "v4timing=1 ")):
        raise ValueError("diagnostic report")
    fields = {}
    for item in text.split():
        if "=" not in item:
            raise ValueError("diagnostic field")
        key, value = item.split("=", 1)
        if not key or key in fields:
            raise ValueError("diagnostic field")
        fields[key] = value
    kind = "v4health" if text.startswith("v4health=") else "v4timing"
    if fields.get(kind) != "1":
        raise ValueError("diagnostic version")
    if kind == "v4health":
        required = ("boot82_bound", "adc_ok", "adc_missed", "adc_missed_backlog",
                    "adc_backlog_catchup", "adc_sequence_gap", "crc_bad",
                    "spi_errors", "event_depth_max", "frames")
        if fields.get("frames") == "ACQ1":
            raise ValueError("health must not claim ACQ1")
    else:
        required = ("adc_missed", "adc_backlog_catchup", "adc_missed_backlog",
                    "boot82_bound", "spi_min_us", "spi_max_us", "spi_avg_us",
                    "spi_lt40", "spi_ge100", "service_min_us", "service_max_us",
                    "service_avg_us", "service_over_budget", "lag_max_us",
                    "lag_ge250", "event_depth_max", "ads_hwm_words",
                    "led_hwm_words", "sched_hwm_words", "cpu")
    missing = [name for name in required if name not in fields]
    if missing:
        raise ValueError("diagnostic missing " + ",".join(missing))
    return fields

def describe_led_diagnostics(line):
    """Decode cached per-channel masks; never infer light from a valid bit."""
    fields = dict(item.split('=', 1) for item in line.split() if '=' in item)
    names = ('led_ef_valid', 'led_open', 'led_short', 'led_ef_invalid')
    if not all(name in fields for name in names): return None
    masks = [int(fields[name], 16) for name in names]
    if any(mask < 0 or mask > 0xffffff for mask in masks) or any(mask & ~masks[0] for mask in masks[1:]):
        raise ValueError('invalid cached EFLAG masks')
    decoded = {name: [ch for ch in range(24) if mask & (1 << ch)] for name, mask in
               zip(('sampled_pca_channels', 'open_pca_channels', 'short_pca_channels',
                    'invalid_code_pca_channels'), masks)}
    return 'PCA_EFLAG '+json.dumps(decoded)


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
            line=port.readline(1536).decode('utf8',errors='replace').strip()
            if line.startswith('STATUS '):
                print(line)
                try: diagnostic = describe_led_diagnostics(line)
                except ValueError: diagnostic = 'PCA_EFLAG invalid cached diagnostics; retain raw STATUS'
                if diagnostic: print(diagnostic)
                return
            if line.startswith('ERR '): raise RuntimeError(line)
        raise TimeoutError('no complete USB STATUS response')
    finally:
        port.close()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command",choices=list(COMMANDS)+["run"])
    parser.add_argument("--serial-port")
    parser.add_argument("--host",default="192.168.4.1")
    parser.add_argument("--port",type=int,default=3333)
    parser.add_argument("--timeout",type=float,default=0.5)
    parser.add_argument("--retries",type=int,default=2)
    parser.add_argument("--rate",type=int,default=None,
                        help="Frame rate for set-fs, or optional configuration before start/run; integer 2..10 Hz; boot default is5 Hz")
    parser.add_argument("--channel",type=int,default=0)
    parser.add_argument("--current-ma",type=int,default=0)
    parser.add_argument("--seconds",type=float,default=10)
    parser.add_argument("--led",action="store_true",
                        help="Explicitly start real LEDs in the opt-in bring-up profile; requires an approved bench procedure")
    parser.add_argument("--acquisition", action="store_true", help="Require ACQ1 acquisition capability")
    parser.add_argument("--ack-short-warning", action="store_true",
                        help="Acknowledge the unqualified board SHORT advisory candidate")
    args=parser.parse_args()
    client=None
    try:
        if not 1<=args.port<=65535: raise ValueError("UDP port")
        if args.command=="set-fs" and args.rate is None: args.rate=5
        if args.rate is not None and not 2 <= args.rate <= 10: raise ValueError("rate must be an integer from2 to10 Hz")
        if args.command=="set-led-current" and not (0<=args.channel<20 and 0<=args.current_ma<=50): raise ValueError("channel/current")
        if args.command=="run" and (not math.isfinite(args.seconds) or args.seconds <= 0): raise ValueError("duration must be positive and finite")
        client=Client(args.host,args.port,args.timeout,args.retries)
        if args.command in ("start", "run"):
            info = require_ok(client.request("info"))
            print(json.dumps(info), flush=True)  # One cached preflight reply, before START.
            if args.acquisition and "frames=ACQ1" not in info["info"]:
                raise RuntimeError("ACQ1 acquisition is not enabled")
            real_led = "led=REAL_BRINGUP" in info["info"]
            if real_led and not args.led:
                raise RuntimeError("Real LED profile: START can energize emitters. Use --led only under the approved bench procedure")
            if args.led and not real_led:
                raise RuntimeError("Real LED bring-up is not enabled in this firmware")
            if "short_policy=BENCH_WARN" in info["info"]:
                if not (args.led and args.ack_short_warning):
                    raise RuntimeError("SHORT advisory requires --led --ack-short-warning")
                # Respect the running image: old firmware may still advertise a cap.
                capabilities = dict(item.split("=", 1) for item in info["info"].split(";") if "=" in item)
                fs_min = int(capabilities.get("fs_min", "5"))
                fs_max = int(capabilities.get("fs_max", "5"))
                if args.rate is not None and not fs_min <= args.rate <= fs_max:
                    raise ValueError(f"Running firmware advertises Fs{fs_min}..{fs_max}; Owner build required for Fs2..10")
                limit_s = int(capabilities.get("limit_s", "-1"))
                if limit_s < 0:
                    raise ValueError("SHORT advisory firmware has no valid duration capability")
                if args.command == "run" and limit_s and args.seconds > limit_s:
                    raise ValueError(f"Running firmware advertises limit_s={limit_s}; Owner build required for host-controlled duration")
            if args.rate is not None:
                require_ok(client.request("set-fs", bytes([args.rate])))
        if args.command=="run":
            client.frame_sink = lambda frame: print(json.dumps(frame), flush=True)
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
