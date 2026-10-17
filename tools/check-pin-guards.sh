#!/usr/bin/env bash
# Class S pin guards for the ADS131 path (issue #35).
# GPIO26 / SPICS1 must not appear in active firmware.
# Pin numbers stay in main/nn2_board_v1.c, not in the adapter.
# IO35-37 fail the check when octal flash or PSRAM is enabled.
set -eu
ROOT="${1:-.}"
cd "$ROOT"
fail=0

if grep -RInE --include='*.c' --include='*.h' \
    'GPIO_NUM_26|GPIO26|SPICS1|esp_efuse_write|esp_efuse_batch_write_begin|espefuse' \
    components main; then
    echo "FAIL: GPIO26, SPICS1, or an eFuse write is referenced"
    fail=1
fi

pin_re='GPIO_NUM_(26|35|36|37|38)|IO(26|35|36|37|38)|=[[:space:]]*(26|35|36|37|38)([^0-9]|$)'
if grep -RInE --include='*.c' --include='*.h' "$pin_re" components/nn2_adapter_ads131; then
    echo "FAIL: pin literal in the ADS131 adapter"
    fail=1
fi

tmp="$(mktemp)"
grep -RInE --include='*.c' --include='*.h' "$pin_re" components main > "$tmp" || true
if grep -v 'main/nn2_board_v1.c' "$tmp" | grep -q .; then
    echo "FAIL: IO35-37 pin assignment outside the board pin table"
    grep -v 'main/nn2_board_v1.c' "$tmp"
    fail=1
fi
rm -f "$tmp"

octal=0
for f in sdkconfig.defaults sdkconfig; do
    [ -f "$f" ] || continue
    if grep -qE '^CONFIG_SPIRAM=y$|^CONFIG_ESPTOOLPY_OCT_FLASH=y$|^CONFIG_SPIRAM_MODE_OCT=y$' "$f"; then
        octal=1
        echo "octal flash or PSRAM is enabled in $f"
    fi
done
if [ "$octal" -eq 1 ]; then
    if grep -RInE --include='*.c' --include='*.h' "$pin_re" components main; then
        echo "FAIL: IO35-37 used while octal flash or PSRAM is enabled"
        fail=1
    fi
fi

safe_line="$(grep -n 'safe_state' main/app_main.c | head -n 1 | cut -d: -f1)"
task_line="$(grep -n 'create_task' main/app_main.c | head -n 1 | cut -d: -f1)"
if [ -z "$safe_line" ] || [ -z "$task_line" ] || [ "$safe_line" -ge "$task_line" ]; then
    echo "FAIL: app_main must call safe_state before create_task"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS pin guards"
