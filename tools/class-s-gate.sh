#!/usr/bin/env bash
# Class S gate, ADR-0001 §§3.3,3.5. Exit 0 PASS, 1 FAIL, 3 NOT APPLICABLE.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
"${PYTHON:-python3}" "$HERE/layer_contract.py" paths > /dev/null || exit 1
# N6 PARTIAL: scanner policy migration belongs to PR #43.
"${PYTHON:-python3}" "$HERE/class-s-gate.py" "${1:-.}"
rc=$?
case "$rc" in 0|1|3) exit "$rc" ;; *) echo "FAIL: scanner/interpreter error (exit $rc)"; exit 1 ;; esac
