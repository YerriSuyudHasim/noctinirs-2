#!/usr/bin/env bash
# Class U host tests for the ADS131 port, CRC, ring, timeout, and config.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/nn2-host-tests"
cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
    -I "$ROOT/components/nn2_core/include" \
    -I "$ROOT/components/nn2_ports/include" \
    -I "$ROOT/components/nn2_app/include" \
    -I "$ROOT/components/nn2_rtos/include" \
    -I "$ROOT/components/nn2_adapter_ads131/include" \
    "$ROOT/components/nn2_core/nn2_crc16.c" \
    "$ROOT/components/nn2_core/nn2_frame_ring.c" \
    "$ROOT/components/nn2_app/nn2_app_safe.c" \
    "$ROOT/components/nn2_adapter_ads131/nn2_ads131_logic.c" \
    "$ROOT/test/host/test_ads131.c" \
    -o "$OUT"
"$OUT"

forbidden='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"]([^>"]*/)?(esp_[^>"]*|freertos/[^>"]*|FreeRTOS\.h|driver/[^>"]*|lwip/[^>"]*|hal/[^>"]*|soc/[^>"]*|rom/[^>"]*|sdkconfig\.h|nn2_adapter_[^>"]*)[>"]'
if grep -RInE "$forbidden" \
    "$ROOT/components/nn2_core" \
    "$ROOT/components/nn2_ports" \
    "$ROOT/components/nn2_app"; then
    echo "FAIL: forbidden include in an inner layer"
    exit 1
fi
echo "PASS port include scan"
