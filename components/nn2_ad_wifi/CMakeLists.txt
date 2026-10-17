idf_component_register(
    SRCS "nn2_ad_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES nn2_core nn2_ports nn2_rtos esp_wifi esp_event esp_netif esp_timer freertos
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Werror=undef)
