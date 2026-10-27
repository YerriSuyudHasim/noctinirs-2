idf_component_register(SRCS "nn2_wifi.c" INCLUDE_DIRS "include"
                       REQUIRES nn2_ports esp_common
                       PRIV_REQUIRES nn2_app esp_wifi esp_event esp_netif freertos lwip)
