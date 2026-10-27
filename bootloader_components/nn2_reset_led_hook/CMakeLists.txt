# The IDF bootloader links bootloader_hooks_include to retain the hook object.
idf_component_register(SRCS "nn2_reset_led_hook.c" REQUIRES esp_hal_gpio esp_rom)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../components/nn2_board/include")
