idf_component_register(SRCS "nn2_net.c" INCLUDE_DIRS "include"
                       REQUIRES nn2_app nn2_ports esp_common
                       PRIV_REQUIRES nn2_wifi lwip)
