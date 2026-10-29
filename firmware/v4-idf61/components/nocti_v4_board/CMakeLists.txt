idf_component_register(
    SRCS "nocti_v4_board.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_gpio nocti_v4_common
)
