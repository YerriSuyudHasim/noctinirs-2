# Ports layer (ADR-0001 3.5): header-only contracts, REQUIRES nn2_core only.
idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES nn2_core)
