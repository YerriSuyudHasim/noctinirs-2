idf_component_register(
    SRCS "nocti_v3_timebase.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_gptimer nocti_v3_common
    PRIV_REQUIRES log
)
