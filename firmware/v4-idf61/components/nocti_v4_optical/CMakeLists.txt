idf_component_register(
    SRCS "nocti_v4_optical.c"
    INCLUDE_DIRS "include"
    REQUIRES
        nn2_core
        freertos
        esp_common
        nocti_v4_common
        nocti_v4_ads
        nocti_v4_scheduler
        nocti_v4_health
        nocti_v4_time
)
