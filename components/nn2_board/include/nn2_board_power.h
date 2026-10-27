#ifndef NN2_BOARD_POWER_H
#define NN2_BOARD_POWER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "nn2_port_battery.h"

typedef nn2_port_battery_sample_t nn2_board_power_sample_t;

/* Main task only: init once, then all calls from the same task, never an ISR.
 * Main-board schematic 2026-10-16: SDA15/SCL16, CE8 active low.
 * Init holds emitters in reset, disables CE first, opens synchronous I2C0,
 * checks IC identities and optionally enables charging. Errors leave CE high
 * when GPIO operations succeed. Programs nominal 512 mA / 4176 mV (with nominal
 * tolerance headroom below the Owner's 4200 mV target); device tolerance still applies.
 * Hardware and battery compatibility must be reviewed before running firmware. */
esp_err_t nn2_board_power_init(bool enable_on_boot);

/* Outputs are replaced only after every read succeeds. No implicit polling,
 * retry, explicit bus reset, fuel-gauge quick-start or alert/fault-latch clearing.
 * ESP-IDF may perform its own controller recovery after transfer errors. */
esp_err_t nn2_board_power_read(nn2_board_power_sample_t *sample);

/* Enable: disable CE, configure BQ watchdog/charge mode with readback, then CE low.
 * Disable: GPIO only, available even while the I2C bus is released.
 * Success means a software enable request, not measured charging. */
esp_err_t nn2_board_power_set_charge_enabled(bool enabled);

/* Exclusive bus handoff, not concurrent sharing. Release before LED driver
 * creation. Reacquire ONLY after that driver has drained its transfers, removed
 * its devices and deleted its bus. No power reads are permitted while released.
 * Use I2C0 for BOTH phases; never route I2C1 onto these same wires.
 * A future LED scheduler must enforce this contract for the whole acquisition.
 * Release preserves CE and never touches PCA9956B registers or RESET_LED. */
esp_err_t nn2_board_power_release_i2c(void);
esp_err_t nn2_board_power_acquire_i2c(void);

/* Adapter for the application monitor. Acquires, reads and releases the bus on
 * each call. Scheduler must authorize the exclusive idle window beforehand. */
nn2_port_battery_t nn2_board_battery_port(void);

#endif
