#!/usr/bin/env bash
# Class U host tests for the M2 Wi-Fi provision policy. No ESP-IDF, no hardware.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/nn2-host-tests"
mkdir -p "$OUT"

fail() { echo "FAIL: $*" >&2; exit 1; }

# Source scan: adapters and composition must not carry these tokens.
SCAN_DIRS=(
  "$ROOT/components/nn2_core"
  "$ROOT/components/nn2_ports"
  "$ROOT/components/nn2_app"
  "$ROOT/main"
)
for extra in nn2_ad_wifi nn2_ad_cred nn2_ad_prov nn2_rtos; do
  if [ -d "$ROOT/components/$extra" ]; then
    SCAN_DIRS+=("$ROOT/components/$extra")
  fi
done
if grep -RInE --include='*.c' --include='*.h' 'portMAX_DELAY|safe_off|SafeOff|nvs_flash_erase|WIFI_MODE_AP' "${SCAN_DIRS[@]}" ; then
  fail "forbidden token in active firmware sources"
fi

cc_one() {
  gcc -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/components/nn2_core/include" \
    -I"$ROOT/components/nn2_ports/include" \
    -I"$ROOT/components/nn2_app/include" \
    -I"$ROOT/components/nn2_rtos/include" \
    -I"$ROOT/test/host/unity" \
    -c "$1" -o "$2"
}

cc_one "$ROOT/components/nn2_core/nn2_wifi_cred.c" "$OUT/cred.o"
cc_one "$ROOT/components/nn2_core/nn2_wifi_backoff.c" "$OUT/backoff.o"
cc_one "$ROOT/components/nn2_app/nn2_app_wifi.c" "$OUT/app.o"
cc_one "$ROOT/test/host/test_cred.c" "$OUT/test_cred.o"
cc_one "$ROOT/test/host/test_backoff.c" "$OUT/test_backoff.o"
cc_one "$ROOT/test/host/test_link.c" "$OUT/test_link.o"
cc_one "$ROOT/test/host/test_rtos.c" "$OUT/test_rtos.o"
cc_one "$ROOT/test/host/test_runner.c" "$OUT/test_runner.o"
gcc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT/test/host/unity" \
  -c "$ROOT/test/host/unity/unity.c" -o "$OUT/unity.o"
gcc -o "$OUT/nn2_host_tests" \
  "$OUT/cred.o" "$OUT/backoff.o" "$OUT/app.o" \
  "$OUT/test_cred.o" "$OUT/test_backoff.o" "$OUT/test_link.o" \
  "$OUT/test_rtos.o" "$OUT/test_runner.o" "$OUT/unity.o"
"$OUT/nn2_host_tests"
