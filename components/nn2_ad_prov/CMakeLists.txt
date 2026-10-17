idf_component_register(
    SRCS "nn2_ad_prov.c"
    INCLUDE_DIRS "include"
    REQUIRES nn2_core nn2_ports esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Werror=undef)
