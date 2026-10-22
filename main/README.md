# main (composition root + ESP32-S3 adapters)

`app_main.c` reads the reset reason, runs the issue #49 emitter safe-off through `nn2_port_emitter_safe_t`, and only then wires the adapters into `nn2_app` for `CONFIG_NN2_DEMO_CYCLES` acquisition cycles, logging each sample with its provenance (`src=FAKE`). On the first fault it stops cycling; the sequencer has already issued the fail-safe `off()` in this task. A safe-off failure returns before that demo.

## Scope 0.1.x: fake/disabled hardware adapters only
- `adapters/nn2_adapter_led_disabled.c`: LED/emitter **disabled** no-op that only logs. It does not include `driver/gpio.h` and never configures or drives a GPIO.
- `adapters/nn2_adapter_adc_fake.c`: **fake** ADC. It returns a deterministic synthetic `raw=0` tagged `NN2_SAMPLE_SRC_FAKE`. This is not a measurement, and no pin or ADC unit is touched.
- `esp_timer` clock and `esp_log` logger.

The acquisition path still configures and drives no GPIO. The issue #49 boot path drives RESET_LED (IO18) only: the second-stage bootloader holds it low, and the safe-off adapter releases it for the stated off-sequence and then holds it low again. PCA9956B bus parameters that issue #49 does not state fail the target build (`nn2_emitter_safe_hw.h`). At system level, the boot ROM strapping pins, the flash SPI pins, and the UART0 console (GPIO43/44) remain system-level considerations. USB-Serial-JTAG (GPIO19/20, when enabled) and PSRAM pins (GPIO33-37 on octal-PSRAM parts) must also be accounted for; this is not a measured pin-state claim.

The real `adc_oneshot` and GPIO LED code was removed from this branch (Architect F1 on PR #15). It is preserved on branch `wip/m1-hw-adapters-parked`, which must never be merged. Real adapters are deferred until the hardware-contract preconditions in the M1 deferral issue are met.

Adapters live in `main/` because ADR-0001 3.5 treats any unlisted `components/nn2_*` as a layer-map violation.

Kconfig (*NoctiNIRS 2*): `NN2_LED_SETTLE_US` (0..1000 us bounded busy-wait, default 100, pending RTOS review) and `NN2_DEMO_CYCLES`. There is still no Kconfig pin menu. Missing PCA9956B parameters are compile definitions with no default (`HW-TBD (Agent 04)`).

The demo runs synchronously in the ESP-IDF `app_main` caller task. It creates no task or ISR. The 0..1000 us `esp_rom_delay_us` settle wait is a busy wait; `vTaskDelay(pdMS_TO_TICKS(100))` yields between successful cycles. Watchdog/scheduling acceptability requires bounded RTOS review and measurement. FAULT terminates the demo after draining published records, without reset or another `on()`. An injected `off()` success is not physical safe-state evidence. Real emitters remain blocked by #19, including independent cut-off/default-off obligations.
