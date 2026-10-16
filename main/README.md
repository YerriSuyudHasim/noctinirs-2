# main (composition root + ESP32-S3 adapters)

`app_main.c` wires the adapters into `nn2_app` and runs `CONFIG_NN2_DEMO_CYCLES` acquisition cycles, logging each sample with its provenance (`src=FAKE`). On the first fault it stops cycling; the sequencer has already issued the fail-safe `off()` in this task.

## Scope 0.1.x: fake/disabled hardware adapters only
- `adapters/nn2_adapter_led_disabled.c`: LED/emitter **disabled** no-op that only logs. It does not include `driver/gpio.h` and never configures or drives a GPIO.
- `adapters/nn2_adapter_adc_fake.c`: **fake** ADC. It returns a deterministic synthetic `raw=0` tagged `NN2_SAMPLE_SRC_FAKE`. This is not a measurement, and no pin or ADC unit is touched.
- `esp_timer` clock and `esp_log` logger.

NN2 application code configures and drives no GPIO. At system level, the boot ROM strapping pins, the flash SPI pins, and the UART0 console (GPIO43/44) are still active, as they are for any ESP-IDF image.

The real `adc_oneshot` and GPIO LED code was removed from this branch (Architect F1 on PR #15). It is preserved on branch `wip/m1-hw-adapters-parked`, which must never be merged. Real adapters are deferred until the hardware-contract preconditions in the M1 deferral issue are met.

Adapters live in `main/` because ADR-0001 3.5 treats any unlisted `components/nn2_*` as a layer-map violation.

Kconfig (*NoctiNIRS 2*): `NN2_LED_SETTLE_US` (0..1000 us bounded busy-wait, default 100, pending RTOS review) and `NN2_DEMO_CYCLES`. There are no pin or enable options.
