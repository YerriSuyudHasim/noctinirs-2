# main (composition root + ESP32-S3 adapters)

`app_main.c` wires the ESP32-S3 adapters into `nn2_app` and runs `CONFIG_NN2_DEMO_CYCLES` acquisition cycles, logging each raw sample.

Adapters (`adapters/`, ESP-IDF allowed here): `esp_timer` clock, `esp_log` logger, `adc_oneshot` ADC1 sampler, GPIO LED.
They live in `main/` for this draft because ADR-0001 3.5 treats any unlisted `components/nn2_*` as a layer-map violation; moving them to `components/nn2_adapters_<area>` requires an ADR map revision.

Kconfig (`idf.py menuconfig` → *NoctiNIRS 2*): `NN2_LED_ENABLE` (default **n**, no GPIO driven), `NN2_ADC_ENABLE` (default **n**, stub raw=0), pins are placeholders pending Hardware review.
