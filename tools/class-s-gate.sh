#!/usr/bin/env bash
# Class S gate, ADR-0001 §§3.3,3.5. Exit 0 PASS, 1 FAIL, 3 NOT APPLICABLE.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
python3 "$HERE/class-s-gate.py" "${1:-.}"
