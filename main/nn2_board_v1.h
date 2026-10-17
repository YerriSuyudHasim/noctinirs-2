#ifndef NN2_BOARD_V1_H
#define NN2_BOARD_V1_H

#include "nn2_ads131_logic.h"
#include "nn2_port_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TBD(#35). OSR and data rate are composition inputs, not driver
   constants. ADR-0002 alignment of the two is OPEN. 512 and 8000 Hz
   are the historical baseline carried as placeholders only. */
#define NN2_BOARD_ADS131_OSR_TBD35 512u
#define NN2_BOARD_ADS131_RATE_TBD35 8000u

/* Single board pin table. MISO and DRDY follow the netlist and stay
   placeholders until Agent 04 confirms them on the board. The numeric
   assignments live only in nn2_board_v1.c. */
extern const nn2_ads131_pins_t nn2_board_ads131_pins;
extern const nn2_adc_config_t nn2_board_ads131_cfg;

#ifdef __cplusplus
}
#endif

#endif /* NN2_BOARD_V1_H */
