idf_component_register(
    SRCS "nocti_v4_common.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_system
    PRIV_REQUIRES log
)
