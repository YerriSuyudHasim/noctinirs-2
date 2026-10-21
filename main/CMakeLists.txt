# Keep canonical inner layers and default fake/disabled adapters.
set(nn2_sources "app_main.c" "adapters/nn2_adapter_clock_esp.c"
    "adapters/nn2_adapter_log_esp.c" "adapters/nn2_adapter_adc_fake.c"
    "adapters/nn2_adapter_led_disabled.c")
set(nn2_requires nn2_core nn2_ports nn2_app esp_timer log esp_rom freertos)
if(CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC)
    list(APPEND nn2_sources "nn2_board_v1.c" "adapters/nn2_ads131_logic.c" "adapters/nn2_ads131_esp.c")
    list(APPEND nn2_requires esp_driver_spi esp_driver_gpio esp_system)
endif()
idf_component_register(SRCS ${nn2_sources} INCLUDE_DIRS "." "adapters" PRIV_REQUIRES ${nn2_requires})
