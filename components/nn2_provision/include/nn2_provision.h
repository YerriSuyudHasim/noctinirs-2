#ifndef NN2_PROVISION_H
#define NN2_PROVISION_H

#include "esp_err.h"
#include "nn2_port_network_config.h"

typedef void (*nn2_provision_status_fn)(char *out, size_t capacity);
typedef nn2_status_t (*nn2_provision_safe_off_fn)(void);
typedef struct {
    uint32_t rejected_lines, tx_drops, timed_out_lines;
} nn2_provision_status_t;

/* Owns USB Serial/JTAG RX. Do not run a console REPL on the same RX stream.
 * Callback runs in the poll owner; must write a NUL-terminated, secret-free line. */
esp_err_t nn2_provision_init(
    const nn2_port_network_config_t *storage, nn2_provision_status_fn status_callback,
    nn2_provision_safe_off_fn safe_off_callback);
/* At most 64 input bytes, zero-tick USB waits; candidate submission is synchronous.
 * Returns true only when a complete valid WIFI line was accepted this poll; persistence is asynchronous. */
bool nn2_provision_poll(uint64_t now_us);
nn2_provision_status_t nn2_provision_get_status(void);

/* NULL storage selects diagnostics-only mode; WIFI is rejected. */
#endif
