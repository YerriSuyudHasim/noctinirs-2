#ifndef NN2_NET_H
#define NN2_NET_H

#include "esp_err.h"
#include "nn2_app_comm.h"
#include "nn2_port_clock.h"

typedef struct {
    bool socket_open;
    /* Written only by the nn2_net task. send_errors = reply + FRAME send failures
     * (unchanged meaning). frame_send_* count only the FRAME sendto in nn2_net_poll
     * and satisfy frame_send_errors = eagain + enobufs + other. */
    uint32_t received, replied, invalid, socket_errors, send_errors;
    uint32_t frame_send_errors, frame_send_eagain, frame_send_enobufs, frame_send_other;
    int last_errno, frame_last_errno;
} nn2_net_status_t;

/* Serialized owner, borrowed App storage/clock outlive transport.
 * init does not open a socket. poll opens only when IPv4 is ready.
 * link_epoch changes invalidate ownership even if disconnect+reconnect happened
 * between polls. A failed socket also invalidates ownership before reopening. */
esp_err_t nn2_net_init(nn2_app_comm_storage_t *app, const nn2_port_clock_t *clock, uint16_t port);
nn2_status_t nn2_net_poll(bool link_up, uint32_t link_epoch);
nn2_status_t nn2_net_stop(void);
nn2_net_status_t nn2_net_get_status(void);

#endif
