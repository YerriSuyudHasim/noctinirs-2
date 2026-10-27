#ifndef NN2_BOARD_H
#define NN2_BOARD_H

#include "esp_err.h"
#include "nn2_port_safety.h"

typedef struct {
    unsigned reset_reason;
    unsigned emitter_reset_gpio;
    bool reset_asserted_by_software;
} nn2_board_boot_info_t;

/* Call first, once, from app_main. Default communication holds RESET_LED. */
esp_err_t nn2_board_init_safe(void);
esp_err_t nn2_board_hold_emitters_off(void);
/* Opt-in LED adapter only, inside its shared stop/release interlock. */
esp_err_t nn2_board_release_emitters_reset(void);
nn2_port_safety_t nn2_board_safety_port(void);
nn2_board_boot_info_t nn2_board_get_boot_info(void);

#endif
