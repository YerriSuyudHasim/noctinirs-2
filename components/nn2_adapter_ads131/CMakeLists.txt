idf_component_register(
    SRCS "nn2_ads131_logic.c" "nn2_ads131_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES nn2_core nn2_ports nn2_rtos esp_driver_gpio esp_driver_spi esp_timer freertos esp_rom
)
