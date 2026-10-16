idf_component_register(
    SRCS "nocti_v3_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES
        log
        nocti_v3_common
        nocti_v3_board
        nocti_v3_timebase
        nocti_v3_adc
        nocti_v3_led
        nocti_v3_optical
        nocti_v3_health
)
