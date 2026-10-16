# Composition root + ESP32-S3 adapters (ADR-0001: main/ may use everything).
# Adapters live in main/adapters for this draft because ADR-0001 3.5 makes any
# unlisted components/nn2_* directory a layer-map violation (CI FAIL).
idf_component_register(SRCS "app_main.c"
                            "adapters/nn2_adapter_clock_esp.c"
                            "adapters/nn2_adapter_log_esp.c"
                            "adapters/nn2_adapter_adc_esp.c"
                            "adapters/nn2_adapter_led_esp.c"
                       INCLUDE_DIRS "." "adapters"
                       REQUIRES nn2_core nn2_ports nn2_app
                       PRIV_REQUIRES esp_timer log esp_adc esp_driver_gpio esp_rom freertos)
