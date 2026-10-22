idf_component_register(SRCS "nn2_reset_led_hook.c"
                       REQUIRES esp_hal_gpio esp_rom)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../main/adapters")
