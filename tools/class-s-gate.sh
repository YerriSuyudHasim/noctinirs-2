#!/usr/bin/env bash
# Class S forbidden-include gate (ADR-0001 3.5), Refs #10.
# Usage: tools/class-s-gate.sh [ROOT]   (default ROOT=.)
# Exit: 0 PASS, 1 FAIL (violation, empty/missing layer, or scanner error), 3 NOT APPLICABLE (no layer present).
# grep semantics: rc=0 match -> FAIL; rc=1 no match -> PASS; rc>=2 scanner error -> FAIL.
set -uo pipefail
ROOT="${1:-.}"
# Only path discovery changes here; Issue #10 scanner behavior stays on its PR.
TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON="${PYTHON:-python3}"
layer_text="$("$PYTHON" "$TOOLS_DIR/layer_contract.py" paths)" || exit 1
[ -n "$layer_text" ] || { echo "FAIL: empty layer manifest"; exit 1; }
mapfile -t layers <<< "$layer_text"
present=0
for l in "${layers[@]}"; do [ -e "$ROOT/$l" ] && present=1; done
if [ "$present" -eq 0 ]; then
  echo "NOT APPLICABLE: no normative layer directory present"
  exit 3
fi
# Once any layer exists, every layer must exist and contain at least one C/C++ source or header.
for l in "${layers[@]}"; do
  if [ ! -d "$ROOT/$l" ]; then
    echo "FAIL: layer directory missing: $l"; exit 1
  fi
  n="$(find "$ROOT/$l" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) | wc -l)"
  if [ "$n" -eq 0 ]; then
    echo "FAIL: layer directory has no C/C++ sources or headers: $l"; exit 1
  fi
done
pattern='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]([^>"]*/)?(esp_[^>"]*|freertos/[^>"]*|FreeRTOS\.h|driver/[^>"]*|lwip/[^>"]*|hal/[^>"]*|soc/[^>"]*|rom/[^>"]*|xtensa/[^>"]*|nimble/[^>"]*|nvs[^>"/]*\.h|nn2_adapter_[^>"]*|sdkconfig\.h)[>"]'
dirs=(); for l in "${layers[@]}"; do dirs+=("$ROOT/$l"); done
out="$(mktemp)" || { echo "FAIL: mktemp error"; exit 1; }
rc=0
grep -rnE --include='*.c' --include='*.h' --include='*.cpp' --include='*.hpp' "$pattern" "${dirs[@]}" > "$out" || rc=$?
[ -n "${VIOLATIONS_FILE:-}" ] && cp "$out" "$VIOLATIONS_FILE"
if [ "$rc" -eq 0 ]; then echo "FAIL: forbidden platform/adapter includes found"; cat "$out"; rm -f "$out"; exit 1
elif [ "$rc" -eq 1 ]; then rm -f "$out"; echo "PASS: no forbidden includes in ${layers[*]}"; exit 0
else echo "FAIL: grep error (exit $rc); gate result invalid"; exit 1
fi
