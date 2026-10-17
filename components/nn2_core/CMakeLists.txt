idf_component_register(
    SRCS "nn2_wifi_cred.c" "nn2_wifi_backoff.c"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror)
