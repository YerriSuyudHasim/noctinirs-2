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

# Target-only guards must fail closed. Host Class U above leaves ESP_PLATFORM unset.
expect_fail() {
  local name="$1"
  local needle="$2"
  shift 2
  set +e
  "$@" >"$OUT/${name}.log" 2>&1
  local rc=$?
  set -e
  if [ "$rc" -eq 0 ]; then
    fail "$name compiled, expected failure"
  fi
  if ! grep -F "$needle" "$OUT/${name}.log" >/dev/null; then
    cat "$OUT/${name}.log" >&2
    fail "$name did not report: $needle"
  fi
  echo "PASS: $name"
}

EMPTY_SDK="$OUT/empty-sdk"
mkdir -p "$EMPTY_SDK"
: >"$EMPTY_SDK/sdkconfig.h"
expect_fail rtos_missing_sdkconfig "sdkconfig not seen" \
  gcc -std=c11 -Wall -Wextra -Werror -DESP_PLATFORM \
    -I"$EMPTY_SDK" \
    -I"$ROOT/components/nn2_rtos/include" \
    -c "$ROOT/components/nn2_rtos/nn2_rtos_anchor.c" \
    -o "$OUT/rtos_missing_sdkconfig.o"

cat >"$OUT/limits_target.c" <<'EOF'
#include "nn2_wifi_limits.h"
int nn2_limits_host_anchor;
EOF
gcc -std=c11 -Wall -Wextra -Werror -Werror=undef -DESP_PLATFORM \
  -I"$ROOT/components/nn2_core/include" \
  -c "$OUT/limits_target.c" \
  -o "$OUT/limits_core_defaults.o"
echo "PASS: limits_core_defaults"

awk 'BEGIN{p=1} /^#include/{exit} {print}' \
  "$ROOT/components/nn2_ad_wifi/nn2_ad_wifi.c" >"$OUT/ad_wifi_guard.c"
echo "int nn2_ad_wifi_guard_anchor;" >>"$OUT/ad_wifi_guard.c"
expect_fail ad_wifi_missing_macros "NN2_WIFI_BACKOFF_FIRST_MS is required" \
  gcc -std=c11 -Wall -Wextra -Werror -Werror=undef \
    -c "$OUT/ad_wifi_guard.c" \
    -o "$OUT/ad_wifi_missing_macros.o"

GOOD_SDK="$OUT/good-sdk"
mkdir -p "$GOOD_SDK"
cat >"$GOOD_SDK/sdkconfig.h" <<'EOF'
#define CONFIG_IDF_TARGET "esp32s3"
#define CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0 1
#define CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0 1
#define CONFIG_LWIP_TCPIP_TASK_AFFINITY 0
#define CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0 1
#define CONFIG_ESP_TIMER_TASK_AFFINITY 0
EOF
gcc -std=c11 -Wall -Wextra -Werror -Werror=undef -DESP_PLATFORM \
  -I"$GOOD_SDK" \
  -I"$ROOT/components/nn2_rtos/include" \
  -c "$ROOT/components/nn2_rtos/nn2_rtos_anchor.c" \
  -o "$OUT/rtos_good_sdkconfig.o"
echo "PASS: rtos_good_sdkconfig"

gcc -std=c11 -Wall -Wextra -Werror -Werror=undef \
  -DNN2_WIFI_BACKOFF_FIRST_MS=500 \
  -DNN2_WIFI_BACKOFF_MAX_MS=8000 \
  -DNN2_WIFI_BACKOFF_FACTOR=2 \
  -DNN2_WIFI_CONNECT_GUARD_MS=15000 \
  -c "$OUT/ad_wifi_guard.c" \
  -o "$OUT/ad_wifi_injected.o"
echo "PASS: ad_wifi_injected"
