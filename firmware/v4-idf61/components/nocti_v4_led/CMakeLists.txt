idf_component_register(
    SRCS "nocti_v4_led.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_rom esp_timer nocti_v4_board nocti_v4_i2c nocti_v4_health
    PRIV_REQUIRES log nn2_core
)
