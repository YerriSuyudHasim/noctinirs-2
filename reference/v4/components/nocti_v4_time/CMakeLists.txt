idf_component_register(
    SRCS "nocti_v4_time.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gptimer esp_common
)
