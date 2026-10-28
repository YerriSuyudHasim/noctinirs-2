#ifndef NN2_APP_PROVISION_H
#define NN2_APP_PROVISION_H
#include "nn2_port_network_config.h"
bool nn2_app_network_config_valid(const nn2_network_config_t *value);
/* WIFI <2..64 hex SSID digits> <16..126 hex password digits>, no CR/LF.
 * Never echoes input. out changes only after successful save. <=196 bytes.
 * The caller must erase its mutable input buffer after calling. */
nn2_status_t nn2_app_provision(const char *line, size_t length,
    const nn2_port_network_config_t *port, nn2_network_config_t *out);
#endif
