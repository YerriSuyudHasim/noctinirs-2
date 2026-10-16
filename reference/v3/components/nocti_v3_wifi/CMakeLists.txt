idf_component_register(
    SRCS "nocti_v3_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        lwip
        nocti_v3_common
        nocti_v3_stream
        nocti_v3_optical
        nocti_v3_timebase
        nocti_v3_health
)
