#!/usr/bin/env bash
# Fixture self-test for tools/class-s-gate.sh (Refs #10). Exits non-zero if any fixture gives the wrong verdict.
set -uo pipefail
GATE="$(cd "$(dirname "$0")" && pwd)/class-s-gate.sh"
T="$(mktemp -d)"; fails=0
mk() { d="$T/$1"; mkdir -p "$d/components/nn2_core/src" "$d/components/nn2_ports/include" "$d/components/nn2_app/src"
  echo '#include <stdint.h>' > "$d/components/nn2_core/src/a.c"
  echo '#include "nn2_core_types.h"' > "$d/components/nn2_ports/include/p.h"
  echo '#include "nn2_port_led.h"' > "$d/components/nn2_app/src/b.c"; }
expect() { name="$1"; want="$2"; shift 2; out="$(bash "$GATE" "$@" 2>&1)"; got=$?
  if [ "$got" -eq "$want" ]; then echo "ok   $name (exit $got)"; else echo "FAIL $name: want $want got $got: $out"; fails=$((fails+1)); fi; }
mk valid;            expect valid-header-only-ports 0 "$T/valid"
mkdir -p "$T/none";  expect no-layers-not-applicable 3 "$T/none"
for inc in '<esp_log.h>' '<freertos/task.h>' '"FreeRTOS.h"' '<driver/gpio.h>' '<rom/ets_sys.h>' '<esp32s3/rom/ets_sys.h>' '<xtensa/hal.h>' '<nvs.h>' '<nvs_flash.h>' '<nimble/ble.h>' '"nn2_adapter_esp32s3.h"' '"sdkconfig.h"' '<soc/gpio_reg.h>' '<hal/gpio_hal.h>' '<lwip/sockets.h>'; do
  n="forbid-$(echo "$inc" | tr -c 'a-zA-Z0-9' '_')"; mk "$n"; echo "#include $inc" >> "$T/$n/components/nn2_app/src/b.c"; expect "$n" 1 "$T/$n"
done
mk empty;   rm -rf "$T/empty/components/nn2_app"/*;  expect empty-layer-folder 1 "$T/empty"
mk missing; rm -rf "$T/missing/components/nn2_ports"; expect missing-layer-folder 1 "$T/missing"
mk scanerr; chmod 000 "$T/scanerr/components/nn2_core/src"
if [ "$(id -u)" -ne 0 ]; then expect scanner-error 1 "$T/scanerr"; else echo "skip scanner-error (root)"; fi
chmod 755 "$T/scanerr/components/nn2_core/src"
rm -rf "$T"
echo "selftest failures: $fails"; [ "$fails" -eq 0 ]
