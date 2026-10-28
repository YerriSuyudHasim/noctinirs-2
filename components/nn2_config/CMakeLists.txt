idf_component_register(SRCS "nn2_config.c" INCLUDE_DIRS "include"
                       REQUIRES nn2_ports esp_common
                       PRIV_REQUIRES nn2_app nvs_flash)
