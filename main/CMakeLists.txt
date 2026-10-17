idf_component_register(
    SRCS "app_main.c" "nn2_board_v1.c"
    INCLUDE_DIRS "."
    REQUIRES nn2_adapter_ads131 nn2_app nn2_ports nn2_core esp_driver_spi
)
