# Core layer (ADR-0001 3.5): pure C11, no ESP-IDF includes, REQUIRES nothing.
idf_component_register(SRCS "src/nn2_core_ring.c" "src/nn2_core_status.c" "nn2_wifi_cred.c" "nn2_wifi_backoff.c"
                       INCLUDE_DIRS "include")
