idf_component_register(
    SRCS "nocti_v4_network.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        esp_event
        esp_netif
        esp_wifi
        freertos
        heap
        lwip
        nvs_flash
        nocti_v4_common
        nocti_v4_health
        nocti_v4_optical
        nocti_v4_time
)
