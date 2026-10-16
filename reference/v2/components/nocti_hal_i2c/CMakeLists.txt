# ============================================================
# File: components/nocti_hal_i2c/CMakeLists.txt
# ============================================================

idf_component_register(
    SRCS
        "nocti_hal_i2c.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_common
        freertos
        esp_driver_i2c
        nocti_common
        nocti_timebase
    PRIV_REQUIRES
        driver
        log
)
