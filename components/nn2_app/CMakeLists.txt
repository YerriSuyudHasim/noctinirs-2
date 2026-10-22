# Application layer (ADR-0001 3.5): REQUIRES nn2_core nn2_ports only.
idf_component_register(SRCS "src/nn2_app_sequencer.c" "src/nn2_app_adc_diagnostic.c"
                       "src/nn2_app_safe_boot.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nn2_core nn2_ports)
