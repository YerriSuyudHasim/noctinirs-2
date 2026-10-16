idf_component_register(
    SRCS "nocti_v4_i2c.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c nocti_v4_board nocti_v4_health
)
