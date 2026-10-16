# ============================================================
# File: components/nocti_biopot/CMakeLists.txt
# ============================================================

idf_component_register(
    SRCS
        "nocti_biopot.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        freertos
        log
        nocti_timebase
        nocti_hal_spi
        nocti_common
)
