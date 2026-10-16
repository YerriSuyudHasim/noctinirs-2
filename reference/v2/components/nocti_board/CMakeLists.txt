# ============================================================
# File: components/nocti_board/CMakeLists.txt
# ============================================================

idf_component_register(
    SRCS
        "nocti_board.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        freertos
        log
        nocti_common
        nocti_hal_i2c
        nocti_hal_spi
        nocti_fnirs
)
