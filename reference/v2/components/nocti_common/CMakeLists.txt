# ============================================================
# File: components/nocti_common/CMakeLists.txt
# ============================================================
# Tujuan file ini
# - Mendaftarkan komponen nocti_common ke ESP-IDF build system
# - Export header di folder include/
# - Compile source utilitas: nocti_crc.c dan nocti_ringbuf.c

idf_component_register(
    SRCS
        "nocti_crc.c"
        "nocti_ringbuf.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        log
)
