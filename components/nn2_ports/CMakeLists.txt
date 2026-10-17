idf_component_register(
    SRCS "nn2_ports_anchor.c"
    INCLUDE_DIRS "include"
    REQUIRES nn2_core
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror -Werror=undef)
