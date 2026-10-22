# Ports layer (ADR-0001 3.5): REQUIRES nn2_core only.
# The anchor keeps the Wi-Fi port headers in one translation unit.
idf_component_register(SRCS "nn2_ports_anchor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nn2_core)
