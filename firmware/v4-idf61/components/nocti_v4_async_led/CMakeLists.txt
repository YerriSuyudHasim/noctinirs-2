idf_component_register(
    SRCS "nocti_v4_async_led.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        esp_driver_i2c
        nocti_v4_board
        nocti_v4_common
        nocti_v4_health
        nocti_v4_i2c
        nocti_v4_led
        nocti_v4_time
)
