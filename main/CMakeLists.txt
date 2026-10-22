# Keep canonical inner layers and default fake/disabled adapters.
set(nn2_sources "app_main.c" "adapters/nn2_adapter_clock_esp.c"
    "adapters/nn2_adapter_log_esp.c" "adapters/nn2_adapter_adc_fake.c"
    "adapters/nn2_adapter_led_disabled.c")
# IDF discovers requirements before CONFIG_* exists. Keep platform dependencies
# unconditional here; only diagnostic source files and runtime are selected below.
set(nn2_requires nn2_core nn2_ports nn2_app esp_timer log esp_rom freertos
    esp_driver_spi esp_driver_gpio esp_system esp_app_format)
if(CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC)
    list(APPEND nn2_sources "nn2_board_v1.c" "adapters/nn2_ads131_logic.c" "adapters/nn2_ads131_esp.c")
endif()
idf_component_register(SRCS ${nn2_sources} INCLUDE_DIRS "." "adapters" PRIV_REQUIRES ${nn2_requires})

execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE nn2_source_sha OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
target_compile_definitions(${COMPONENT_LIB} PRIVATE NN2_SOURCE_SHA="${nn2_source_sha}")
