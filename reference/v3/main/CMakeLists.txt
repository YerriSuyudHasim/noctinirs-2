idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        freertos
        log
        nocti_v3_common
        nocti_v3_board
        nocti_v3_timebase
        nocti_v3_adc
        nocti_v3_led
        nocti_v3_optical
        nocti_v3_stream
        nocti_v3_wifi
        nocti_v3_health
)
