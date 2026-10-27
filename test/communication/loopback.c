/* Real UDP socket around the exact production C protocol/state implementation. */
#include "nn2_app_comm.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
typedef SOCKET socket_t;
typedef int socket_len_t;
#define CLOSE closesocket
static uint64_t now_us(void)
{
    return GetTickCount64() * UINT64_C(1000);
}
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
typedef int socket_t;
typedef socklen_t socket_len_t;
#define CLOSE close
static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000) + (uint64_t)ts.tv_nsec / 1000u;
}
#endif
static nn2_status_t fake_off(void *ctx)
{
    (void)ctx;
    return NN2_OK;
}
int main(int argc, char **argv)
{
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w)) {
        return 2;
    }
#endif
    socket_t fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    unsigned port = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0;
    if (port > 65535) {
        return 2;
    }
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        return 3;
    }
    socket_len_t alen = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &alen)) {
        return 4;
    }
    printf("%u\n", (unsigned)ntohs(addr.sin_port));
    fflush(stdout);
    nn2_app_comm_storage_t state;
    nn2_port_safety_t safety = { .hold_off = fake_off };
    if (nn2_app_comm_init(&state, UINT64_C(0x1122334455667788), &safety, 12345) != NN2_OK) {
        return 6;
    }
    uint8_t input[2048], output[NN2_CORE_PACKET_MAX];
    for (;;) {
        if (nn2_app_comm_tick(&state, now_us(), true) != NN2_OK) {
            return 7;
        }
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(fd, &reads);
        struct timeval wait = { .tv_sec = 0, .tv_usec = 10000 };
        int ready = select((int)fd + 1, &reads, NULL, NULL, &wait);
        if (ready < 0) {
            break;
        }
        if (!ready) {
            continue;
        }
        struct sockaddr_in source;
        socket_len_t n = sizeof(source);
        int got
            = (int)recvfrom(fd, (char *)input, sizeof(input), 0, (struct sockaddr *)&source, &n);
        if (got < 0) {
            break;
        }
        uint64_t peer = ((uint64_t)source.sin_addr.s_addr << 16) | source.sin_port;
        size_t count = nn2_app_comm_process(
            &state, peer, now_us(), input, (size_t)got, output, sizeof(output));
        nn2_app_comm_stamp_tx(output, count, now_us());
        if (count
            && sendto(fd, (const char *)output, (int)count, 0, (struct sockaddr *)&source, n)
                != (int)count) {
            break;
        }
    }
    CLOSE(fd);
    return 5;
}
