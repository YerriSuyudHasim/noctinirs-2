#!/usr/bin/env python3
"""Fail fast when a generated sdkconfig can hide NoctiNIRSv3 INFO logs."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

REQUIRED = {
    "CONFIG_LOG_DEFAULT_LEVEL": "3",
    "CONFIG_LOG_MAXIMUM_LEVEL": "3",
    "CONFIG_LOG_DYNAMIC_LEVEL_CONTROL": "y",
    "CONFIG_LOG_MASTER_LEVEL": "y",
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG": "y",
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED": "y",
    "CONFIG_ESP_CONSOLE_SECONDARY_NONE": "y",
    "CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM": "4",
}


def read_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("sdkconfig", nargs="?", default="sdkconfig")
    args = parser.parse_args()
    path = Path(args.sdkconfig)
    if not path.is_file():
        print(f"FAIL missing sdkconfig: {path}", file=sys.stderr)
        return 2
    values = read_config(path)
    failures: list[str] = []
    for key, expected in REQUIRED.items():
        actual = values.get(key, "<unset>")
        if actual != expected:
            failures.append(f"{key}: expected {expected}, got {actual}")
    if failures:
        print("FAIL NoctiNIRSv3 serial logging configuration", file=sys.stderr)
        for item in failures:
            print(f"  - {item}", file=sys.stderr)
        return 1
    print("PASS logging config: INFO compiled, dynamic/master enabled, USB Serial/JTAG primary")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
