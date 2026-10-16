idf_component_register(
    SRCS "nocti_v3_optical.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        nocti_v3_common
        nocti_v3_adc
        nocti_v3_led
        nocti_v3_timebase
        nocti_v3_health
)
