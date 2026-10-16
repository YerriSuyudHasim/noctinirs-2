# Composition root + ESP32-S3 adapters (ADR-0001: main/ may use everything).
# Adapters live in main/adapters for this draft because ADR-0001 3.5 makes any
# unlisted components/nn2_* directory a layer-map violation (CI FAIL).
# 0.1.x scope: hardware adapters are fake/disabled only, so no esp_adc and no
# esp_driver_gpio dependency (PR #15, Architect F1).
idf_component_register(SRCS "app_main.c"
                            "adapters/nn2_adapter_clock_esp.c"
                            "adapters/nn2_adapter_log_esp.c"
                            "adapters/nn2_adapter_adc_fake.c"
                            "adapters/nn2_adapter_led_disabled.c"
                       INCLUDE_DIRS "." "adapters"
                       PRIV_REQUIRES nn2_core nn2_ports nn2_app
                                     esp_timer log esp_rom freertos)
