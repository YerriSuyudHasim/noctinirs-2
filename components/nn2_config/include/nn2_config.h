#ifndef NN2_CONFIG_H
#define NN2_CONFIG_H

#include "esp_err.h"
#include "nn2_port_network_config.h"

esp_err_t nn2_config_init(void);
/* NOT_FOUND is an unprovisioned board; other errors must remain visible.
 * Outputs are unchanged on error. Caller erases credential copies after use. */
esp_err_t nn2_config_load_network(nn2_network_config_t *out);
esp_err_t nn2_config_save_network(const nn2_network_config_t *value);
nn2_port_network_config_t nn2_config_storage_port(void);
/* Persisted monotonic boot identity, scoped to this board and NVS history.
 * NVS erasure/restore starts a new identity history; not a security token. */
esp_err_t nn2_config_next_boot_id(uint64_t *out);

#endif
