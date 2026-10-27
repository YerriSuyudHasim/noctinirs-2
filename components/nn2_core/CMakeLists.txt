# Core layer (ADR-0001 3.5): pure C11, no ESP-IDF includes, REQUIRES nothing.
idf_component_register(SRCS "src/nn2_core_ring.c" "src/nn2_core_status.c" "src/nn2_core_protocol.c"
                       INCLUDE_DIRS "include")
