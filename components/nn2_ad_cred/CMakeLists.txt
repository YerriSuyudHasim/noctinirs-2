idf_component_register(
    SRCS "nn2_ad_cred.c"
    INCLUDE_DIRS "include"
    REQUIRES nn2_core nn2_ports nn2_rtos nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Werror=undef)
