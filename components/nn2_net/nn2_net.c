#include "nn2_net.h"
#include "nn2_wifi.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static nn2_app_comm_storage_t *s_app;
static nn2_port_clock_t s_clock;
static nn2_net_status_t s_status;
static int s_socket = -1;
static uint16_t s_port;
static uint32_t s_epoch;
static uint64_t s_next_open;

esp_err_t nn2_net_init(nn2_app_comm_storage_t *app, const nn2_port_clock_t *clock, uint16_t port)
{
    if (!app || !clock || !clock->now_us || port < 1024) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_app) {
        return ESP_ERR_INVALID_STATE;
    }
    s_app = app;
    s_clock = *clock;
    s_port = port;
    return ESP_OK;
}

static void close_socket(void)
{
    if (s_socket >= 0) {
        close(s_socket);
        s_socket = -1;
    }
    s_status.socket_open = false;
}

static bool open_socket(void)
{
    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_socket < 0) {
        return false;
    }
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(s_port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (nn2_wifi_bind_socket(s_socket) != ESP_OK || fcntl(s_socket, F_SETFL, O_NONBLOCK) < 0
        || bind(s_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        int saved_errno = errno;
        close_socket();
        errno = saved_errno;
        return false;
    }
    s_status.socket_open = true;
    return true;
}

nn2_status_t nn2_net_poll(bool link_up, uint32_t epoch)
{
    if (!s_app) {
        return NN2_ERR_INVALID_STATE;
    }
    uint64_t now = s_clock.now_us(s_clock.ctx);
    bool changed = epoch != s_epoch;
    s_epoch = epoch;
    nn2_status_t result = nn2_app_comm_tick(s_app, now, link_up && !changed);
    if (result != NN2_OK || !link_up || changed) {
        close_socket();
        if (result != NN2_OK || !link_up) {
            return result;
        }
    }
    if (s_socket < 0 && now >= s_next_open) {
        s_next_open = now + UINT64_C(1000000);
        if (!open_socket()) {
            s_status.last_errno = errno;
            ++s_status.socket_errors;
        }
    }
    for (unsigned budget = 0; s_socket >= 0 && budget < 4; ++budget) {
        uint8_t input[NN2_CORE_PACKET_MAX + 1], output[NN2_CORE_PACKET_MAX];
        struct sockaddr_in source;
        socklen_t source_length = sizeof(source);
        int received = recvfrom(
            s_socket, input, sizeof(input), 0, (struct sockaddr *)&source, &source_length);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            s_status.last_errno = errno;
            ++s_status.socket_errors;
            close_socket();
            return nn2_app_comm_tick(s_app, s_clock.now_us(s_clock.ctx), false);
        }
        ++s_status.received;
        uint64_t peer = ((uint64_t)source.sin_addr.s_addr << 16) | source.sin_port;
        size_t count = nn2_app_comm_process(s_app, peer, s_clock.now_us(s_clock.ctx), input,
            (size_t)received, output, sizeof(output));
        if (!count) {
            ++s_status.invalid;
            continue;
        }
        nn2_app_comm_stamp_tx(output, count, s_clock.now_us(s_clock.ctx));
        int sent = sendto(
            s_socket, output, count, MSG_DONTWAIT, (struct sockaddr *)&source, source_length);
        if (sent != (int)count) {
            s_status.last_errno = sent < 0 ? errno : EIO;
            ++s_status.send_errors;
            /* A full socket TX buffer drops this reply; retry uses the cache.
             * Other errors close transport and revoke the current owner. */
            if (sent >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS)) {
                close_socket();
                return nn2_app_comm_tick(s_app, s_clock.now_us(s_clock.ctx), false);
            }
        } else {
            ++s_status.replied;
        }
    }
    return NN2_OK;
}

nn2_status_t nn2_net_stop(void)
{
    close_socket();
    return s_app ? nn2_app_comm_tick(s_app, s_clock.now_us(s_clock.ctx), false) : NN2_OK;
}

nn2_net_status_t nn2_net_get_status(void)
{
    return s_status;
}
