idf_component_register(
    SRCS "nocti_v3_board.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        nocti_v3_common
    PRIV_REQUIRES log
)
