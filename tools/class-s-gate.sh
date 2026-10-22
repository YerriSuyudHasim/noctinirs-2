#!/usr/bin/env bash
# Class S gate, ADR-0001 §§3.3, 3.5, and Proposed §3.5.1.
# Exit 0 PASS, 1 FAIL, 3 NOT APPLICABLE.
# The Python scanner is the only policy engine. Schema v2 outer rows are
# adapter (nn2_ad_*), platform (nn2_rtos), and composition (main).
# REQUIRES and PRIV_REQUIRES inside if/elseif/else are the union of every
# branch. An unresolved ${...} in a dependency position fails closed.
# target_compile_definitions and target_compile_options carry no dependency.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
rc=0
"${PYTHON:-python3}" "$HERE/class-s-gate.py" "${1:-.}" || rc=$?
case "$rc" in
  0|1|3) exit "$rc" ;;
  *) echo "FAIL: gate interpreter error (exit $rc)"; exit 1 ;;
esac
