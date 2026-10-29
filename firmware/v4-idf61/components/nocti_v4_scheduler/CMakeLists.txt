idf_component_register(
    SRCS "nocti_v4_scheduler.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        esp_common
        esp_driver_gptimer
        nocti_v4_common
        nocti_v4_ads
        nocti_v4_async_led
        nocti_v4_health
        nocti_v4_time
)
