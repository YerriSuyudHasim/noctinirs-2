idf_component_register(
    SRCS "nn2_wifi_cred.c" "nn2_wifi_backoff.c"
    INCLUDE_DIRS "include"
)

# Kconfig integers are project-wide CMake variables. This component has
# no REQUIRES list and its headers do not include sdkconfig.h.
if(NOT CONFIG_NN2_WIFI_BACKOFF_FIRST_MS)
    message(FATAL_ERROR "CONFIG_NN2_WIFI_BACKOFF_FIRST_MS is required")
endif()
if(NOT CONFIG_NN2_WIFI_BACKOFF_MAX_MS)
    message(FATAL_ERROR "CONFIG_NN2_WIFI_BACKOFF_MAX_MS is required")
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    NN2_WIFI_BACKOFF_FIRST_MS=${CONFIG_NN2_WIFI_BACKOFF_FIRST_MS}
    NN2_WIFI_BACKOFF_MAX_MS=${CONFIG_NN2_WIFI_BACKOFF_MAX_MS}
    NN2_WIFI_BACKOFF_FACTOR=2
    NN2_WIFI_CONNECT_GUARD_MS=15000
)

target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror -Werror=undef)
