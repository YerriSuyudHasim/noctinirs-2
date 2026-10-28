# Composition root; existing fake/diagnostic profiles are preserved separately.
set(nn2_sources "app_main.c" "adapters/nn2_adapter_clock_esp.c"
    "adapters/nn2_adapter_log_esp.c" "adapters/nn2_adapter_adc_fake.c"
    "adapters/nn2_adapter_led_disabled.c")
# IDF discovers requirements before CONFIG_* exists. Keep platform dependencies
# unconditional here; only diagnostic source files and runtime are selected below.
set(nn2_requires nn2_core nn2_ports nn2_app esp_timer log esp_rom freertos
    esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_gptimer esp_system esp_app_format esp_hw_support
    nn2_board nn2_config nn2_provision nn2_wifi nn2_net)
if(CONFIG_NN2_PROFILE_COMMUNICATION)
    if(NOT IDF_TARGET STREQUAL "esp32s3")
        message(FATAL_ERROR "Communication Foundation requires IDF_TARGET=esp32s3")
    endif()
    if(CONFIG_ESP_MAIN_TASK_STACK_SIZE LESS 12288)
        message(FATAL_ERROR "Communication profile requires CONFIG_ESP_MAIN_TASK_STACK_SIZE >=12288 in the active sdkconfig. Existing sdkconfig values override sdkconfig.defaults; set Main task stack size in menuconfig or use a fresh configuration with sdkconfig.communication.")
    endif()
else()
    list(APPEND nn2_sources "nn2_legacy_profiles.c")
endif()
if(CONFIG_NN2_LED_BRINGUP)
    list(APPEND nn2_sources "adapters/nn2_led_bringup.c")
endif()
if(CONFIG_NN2_OPTICAL_ACQUISITION)
    list(APPEND nn2_sources "adapters/nn2_acquisition_esp.c")
endif()
if(CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC OR CONFIG_NN2_OPTICAL_ACQUISITION)
    list(APPEND nn2_sources "nn2_board_v1.c" "adapters/nn2_ads131_logic.c" "adapters/nn2_ads131_esp.c")
endif()
idf_component_register(SRCS ${nn2_sources} INCLUDE_DIRS "." "adapters" PRIV_REQUIRES ${nn2_requires})

execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE nn2_source_sha OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
target_compile_definitions(${COMPONENT_LIB} PRIVATE NN2_SOURCE_SHA="${nn2_source_sha}")
