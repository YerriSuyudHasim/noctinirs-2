# ============================================================
# File: components/nocti_hal_spi/CMakeLists.txt
# ============================================================

idf_component_register(
    SRCS
        "nocti_hal_spi.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_common
        driver
        freertos
        nocti_common
    PRIV_REQUIRES
        log
)
