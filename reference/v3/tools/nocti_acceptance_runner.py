#!/usr/bin/env python3
"""Hardware-facing acceptance runner for NoctiNIRSv3.

This tool does not simulate a pass. It records command replies and evaluates only
criteria observable through STATUS/HEALTH. Logic-analyzer and fault-injection
steps remain manual and are identified in the generated report.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, asdict
import json
from pathlib import Path
import re
import socket
import threading
import time
from typing import Iterable

from nocti_protocol import ProtocolError, decode_payload, parse_record

KV_RE = re.compile(r"([A-Za-z0-9_]+)=([^\s]+)")


def parse_kv(reply: str) -> dict[str, str]:
    return {key: value for key, value in KV_RE.findall(reply)}


def as_int(values: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(values.get(key, str(default)), 0)
    except ValueError:
        return default


@dataclass
class Check:
    name: str
    passed: bool
    observed: int | str
    expected: str


class StreamCollector(threading.Thread):
    def __init__(self, bind: str, port: int) -> None:
        super().__init__(daemon=True)
        self.bind_address = bind
        self.port = port
        self.stop_event = threading.Event()
        self.counts: dict[str, int] = {}
        self.decode_errors = 0
        self.crc_bad = 0
        self.complete_phases: dict[tuple[int, int], set[str]] = {}
        self.active_session = 0
        self.ready = threading.Event()

    def run(self) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
            sock.bind((self.bind_address, self.port))
            sock.settimeout(0.25)
            self.ready.set()
            while not self.stop_event.is_set():
                try:
                    datagram, _ = sock.recvfrom(2048)
                except socket.timeout:
                    continue
                try:
                    record = parse_record(datagram)
                    payload = decode_payload(record)
                except ProtocolError:
                    self.decode_errors += 1
                    continue
                self.counts[record.type_name] = self.counts.get(record.type_name, 0) + 1
                if not record.crc_valid:
                    self.crc_bad += 1
                if record.type_name == "SESSION":
                    self.active_session = int(payload["session_id"])
                elif record.type_name == "FNIRS_FRAME":
                    key = (self.active_session, int(payload["frame_id"]))
                    self.complete_phases.setdefault(key, set()).add(str(payload["phase"]))

    def stop(self) -> None:
        self.stop_event.set()
        self.join(timeout=2.0)

    def summary(self) -> dict[str, object]:
        complete = sum(1 for phases in self.complete_phases.values()
                       if phases == {"LED_ON", "I_LED_OFF"})
        return {
            "counts": self.counts,
            "decode_errors": self.decode_errors,
            "crc_bad": self.crc_bad,
            "complete_paired_frames": complete,
            "incomplete_paired_frames": len(self.complete_phases) - complete,
        }


class Device:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.settimeout(timeout)
        self.transcript: list[dict[str, str]] = []

    def close(self) -> None:
        self.socket.close()

    def command(self, command: str) -> str:
        self.socket.sendto(command.encode("ascii"), (self.host, self.port))
        data, _ = self.socket.recvfrom(16384)
        reply = data.decode("utf-8", errors="replace").rstrip()
        self.transcript.append({"command": command, "reply": reply})
        return reply

    def health(self) -> dict[str, str]:
        return parse_kv(self.command("HEALTH"))


def delta(final: dict[str, str], baseline: dict[str, str], key: str) -> int:
    return as_int(final, key) - as_int(baseline, key)


def zero_delta_checks(final: dict[str, str], baseline: dict[str, str], keys: Iterable[str]) -> list[Check]:
    return [Check(key, delta(final, baseline, key) == 0,
                  delta(final, baseline, key), "delta == 0") for key in keys]


def sleep_with_health(device: Device, duration: float, poll: float) -> dict[str, str]:
    deadline = time.monotonic() + duration
    latest: dict[str, str] = {}
    while time.monotonic() < deadline:
        time.sleep(min(poll, max(0.0, deadline - time.monotonic())))
        latest = device.health()
    return latest


def run_dark(device: Device, duration: float, poll: float) -> tuple[list[Check], dict[str, object]]:
    device.command("STOP")
    device.command("RESET_COUNTERS")
    device.command("START DARK")
    time.sleep(1.0)
    baseline = device.health()
    final = sleep_with_health(device, duration, poll)
    device.command("STOP")
    keys = ["adc_missed", "adc_sequence_gap", "drdy_event_overflow",
            "adc_ring_overflow", "crc_bad", "spi_errors"]
    return zero_delta_checks(final, baseline, keys), {"baseline": baseline, "final": final}


def run_scan(device: Device, duration: float, poll: float) -> tuple[list[Check], dict[str, object]]:
    device.command("STOP")
    device.command("RESET_COUNTERS")
    device.command("START SCAN")
    time.sleep(2.0)  # exclude startup transient from delta evaluation
    baseline = device.health()
    final = sleep_with_health(device, duration, poll)
    device.command("STOP")
    checks = zero_delta_checks(final, baseline,
        ["adc_missed", "adc_sequence_gap", "crc_bad", "spi_errors",
         "sched_missed", "sched_abort", "partial_frames", "invalid_frames",
         "underk_on", "underk_off"])
    checks.extend([
        Check("min_n_on", as_int(final, "min_n_on") >= 8,
              as_int(final, "min_n_on"), ">= 8"),
        Check("min_n_off", as_int(final, "min_n_off") >= 8,
              as_int(final, "min_n_off"), ">= 8"),
    ])
    return checks, {"baseline": baseline, "final": final}


def run_startstop(device: Device, cycles: int, dwell: float) -> tuple[list[Check], dict[str, object]]:
    failures: list[str] = []
    device.command("STOP")
    for index in range(cycles):
        start = device.command("START SCAN")
        time.sleep(dwell)
        stop = device.command("STOP")
        status = device.command("STATUS")
        if not start.startswith("START_OK") or not stop.startswith("STOP_OK"):
            failures.append(f"cycle={index + 1} start={start!r} stop={stop!r}")
        values = parse_kv(status)
        if values.get("state") != "STOPPED" or values.get("pca_reset") != "ASSERTED_LOW":
            failures.append(f"cycle={index + 1} unsafe_status={status!r}")
    return [Check("start_stop_cycles", not failures, len(failures), "0 failures")], {"failures": failures}


def run_stress(device: Device, duration: float, rate_hz: float) -> tuple[list[Check], dict[str, object]]:
    device.command("STOP")
    device.command("RESET_COUNTERS")
    device.command("START SCAN")
    time.sleep(2.0)
    baseline = device.health()
    commands = ("STATUS", "HEALTH", "TIMING")
    deadline = time.monotonic() + duration
    sent = 0
    interval = 1.0 / max(rate_hz, 0.1)
    while time.monotonic() < deadline:
        device.command(commands[sent % len(commands)])
        sent += 1
        time.sleep(interval)
    final = device.health()
    device.command("STOP")
    checks = zero_delta_checks(final, baseline,
        ["adc_missed", "adc_sequence_gap", "crc_bad", "spi_errors",
         "sched_missed", "sched_abort"])
    return checks, {"baseline": baseline, "final": final, "commands_sent": sent}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("test", choices=["dark", "scan", "startstop", "stress", "soak", "snapshot"])
    parser.add_argument("--control-port", type=int, default=3333)
    parser.add_argument("--stream-port", type=int, default=3334)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--duration", type=float, default=600.0)
    parser.add_argument("--poll", type=float, default=5.0)
    parser.add_argument("--cycles", type=int, default=100)
    parser.add_argument("--dwell", type=float, default=0.05)
    parser.add_argument("--rate-hz", type=float, default=5.0)
    parser.add_argument("--output", default="NoctiNIRSv3_acceptance_result.json")
    args = parser.parse_args()

    collector = StreamCollector(args.bind, args.stream_port)
    collector.start()
    if not collector.ready.wait(2.0):
        raise SystemExit("stream collector failed to bind")
    device = Device(args.host, args.control_port, args.timeout)
    checks: list[Check] = []
    evidence: dict[str, object] = {}
    try:
        if args.test == "dark":
            checks, evidence = run_dark(device, args.duration, args.poll)
        elif args.test == "scan":
            checks, evidence = run_scan(device, args.duration, args.poll)
        elif args.test == "soak":
            checks, evidence = run_scan(device, args.duration, args.poll)
        elif args.test == "startstop":
            checks, evidence = run_startstop(device, args.cycles, args.dwell)
        elif args.test == "stress":
            checks, evidence = run_stress(device, args.duration, args.rate_hz)
        else:
            evidence = {
                "version": device.command("VERSION"),
                "status": device.command("STATUS"),
                "health": device.command("HEALTH"),
                "timing": device.command("TIMING"),
                "registers": device.command("REGS"),
            }
            checks = [Check("snapshot_collected", True, "yes", "yes")]
    finally:
        collector.stop()
        device.close()

    report = {
        "test": args.test,
        "host": args.host,
        "timestamp_unix": time.time(),
        "passed": all(check.passed for check in checks),
        "checks": [asdict(check) for check in checks],
        "evidence": evidence,
        "stream": collector.summary(),
        "transcript": device.transcript,
        "manual_gates_not_covered": [
            "Wi-Fi physically disabled test",
            "I2C NACK/disconnect fault injection",
            "logic-analyzer waveform/edge timing",
            "400 kHz versus 1 MHz separate-build comparison",
            "memory leak/heap tracing unless separately instrumented",
        ],
    }
    Path(args.output).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
