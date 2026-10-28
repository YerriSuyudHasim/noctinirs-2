#ifndef NN2_WIFI_H
#define NN2_WIFI_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool started;
    bool ap_active;
    bool connected; /* AP running and a station associated, not a UDP lease. */
    uint8_t clients;
    uint32_t link_epoch;
    esp_err_t last_error;
    char ipv4[16];
} nn2_wifi_status_t;
/* Main-task lifecycle owner. Event callbacks only update a locked snapshot.
 * init does not start RF. start starts the password-protected SoftAP. */
esp_err_t nn2_wifi_init(void);
esp_err_t nn2_wifi_start(void);
esp_err_t nn2_wifi_bind_socket(int socket_fd);
nn2_wifi_status_t nn2_wifi_get_status(void);
esp_err_t nn2_wifi_stop(void);
#endif
