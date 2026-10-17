idf_component_register(
    SRCS "nn2_app_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES nn2_core nn2_ports
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror -Werror=undef)
