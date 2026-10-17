idf_component_register(
    SRCS "nn2_crc16.c" "nn2_frame_ring.c"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11)
