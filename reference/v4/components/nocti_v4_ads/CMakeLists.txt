idf_component_register(
    SRCS "nocti_v4_ads.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        esp_driver_gpio
        esp_driver_spi
        esp_rom
        nocti_v4_common
        nocti_v4_board
        nocti_v4_health
        nocti_v4_time
    PRIV_REQUIRES log
)
