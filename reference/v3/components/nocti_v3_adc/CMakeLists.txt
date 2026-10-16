idf_component_register(
    SRCS
        "nocti_v3_adc.c"
        "nocti_v3_ads131m04.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        esp_driver_gpio
        esp_driver_spi
        nocti_v3_common
        nocti_v3_board
        nocti_v3_timebase
        nocti_v3_health
    PRIV_REQUIRES log
)
