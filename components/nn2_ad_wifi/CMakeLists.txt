idf_component_register(
    SRCS "nn2_ad_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES nn2_core nn2_ports nn2_rtos esp_wifi esp_event esp_netif esp_timer freertos
)

if(NOT CONFIG_NN2_WIFI_BACKOFF_FIRST_MS)
    message(FATAL_ERROR "CONFIG_NN2_WIFI_BACKOFF_FIRST_MS is required")
endif()
if(NOT CONFIG_NN2_WIFI_BACKOFF_MAX_MS)
    message(FATAL_ERROR "CONFIG_NN2_WIFI_BACKOFF_MAX_MS is required")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    NN2_WIFI_BACKOFF_FIRST_MS=${CONFIG_NN2_WIFI_BACKOFF_FIRST_MS}
    NN2_WIFI_BACKOFF_MAX_MS=${CONFIG_NN2_WIFI_BACKOFF_MAX_MS}
    NN2_WIFI_BACKOFF_FACTOR=2
    NN2_WIFI_CONNECT_GUARD_MS=15000
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Werror=undef)
