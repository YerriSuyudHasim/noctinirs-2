idf_component_register(SRCS "nn2_board.c" "nn2_board_power.c" "nn2_board_pca.c" INCLUDE_DIRS "include"
                       REQUIRES nn2_ports esp_common esp_driver_i2c
                       PRIV_REQUIRES esp_driver_gpio esp_system freertos)
