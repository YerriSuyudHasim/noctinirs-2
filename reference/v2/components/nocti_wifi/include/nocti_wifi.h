// ============================================================
// File: components/nocti_wifi/include/nocti_wifi.h
// ============================================================
//
// NoctiNIRS WiFi SoftAP + UDP transport
//
// Fitur:
// - SoftAP + DHCP
// - UDP CTRL RX port 3333 (PING, HELLO, START, STOP, INFO)
// - UDP STREAM TX port 3334 (baca dari nocti_stream lalu dibungkus UDP header)
//
// slash:
// /belajar: START akan mengunci target IP pengirim dan mengaktifkan streaming
// /belajar: STOP mematikan streaming dan tidak mengirim data via UDP
// /belajar: UDP stream packet punya seq 32-bit untuk deteksi loss dan session_id untuk pemisah sesi
// ============================================================

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct nocti_stream_t nocti_stream_t;

// ============================================================
// UDP stream packet v1
//
// Tujuan
// - Membuat payload stream (nocti_stream frame) lebih robust di UDP
// - Menyediakan seq 32-bit, session_id, dan timestamp
//
// Konvensi
// - payload = 1 buah nocti_stream frame utuh (hdr 12 + payload + crc16)
// - crc16 = CRC16-CCITT-FALSE dari header tanpa field crc16 + payload
// ============================================================

#define NOCTI_UDP_MAGIC      (0x4E4F4354u)  // slash: 'NOCT'
#define NOCTI_UDP_VER_V1     (1u)

typedef enum {
    NOCTI_UDP_TYPE_INVALID = 0,
    NOCTI_UDP_TYPE_STREAM  = 1,            // slash: payload berisi nocti_stream frame
    NOCTI_UDP_TYPE_STATUS  = 2,            // slash: optional, payload status ringkas
} nocti_udp_type_t;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         // slash: NOCTI_UDP_MAGIC
    uint8_t  ver;           // slash: NOCTI_UDP_VER_V1
    uint8_t  type;          // slash: nocti_udp_type_t
    uint16_t flags;         // slash: reserved, set 0

    uint32_t session_id;    // slash: naik tiap START
    uint32_t seq;           // slash: naik tiap paket stream
    uint64_t timestamp_us;  // common Nocti GPTimer monotonic timebase

    uint16_t payload_len;   // slash: bytes payload
    uint16_t crc16;         // slash: CRC16-CCITT-FALSE
} nocti_udp_hdr_v1_t;
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ip4;   // slash: host order
    uint16_t port;  // slash: host order
} nocti_wifi_peer_t;

typedef struct nocti_wifi_t nocti_wifi_t;

typedef void (*nocti_wifi_cmd_cb_t)(nocti_wifi_t *self,
                                   const uint8_t *data,
                                   size_t len,
                                   const nocti_wifi_peer_t *from,
                                   void *user_ctx);

typedef struct {
    // SoftAP identity
    const char *ap_ssid;
    const char *ap_pass;
    uint8_t ap_channel;
    uint8_t ap_max_conn;

    // Optional custom subnet
    bool use_custom_ip;
    uint32_t ap_ip;       // slash: host order
    uint32_t ap_gw;       // slash: host order
    uint32_t ap_netmask;  // slash: host order

    // UDP ports
    uint16_t udp_ctrl_port;    // slash: default 3333
    uint16_t udp_stream_port;  // slash: default 3334

    // Built in cmd parser
    bool enable_builtin_cmd;

    // Advert broadcast optional
    bool enable_advert;
    uint32_t advert_period_ms;

    // Optional user cmd callback
    nocti_wifi_cmd_cb_t cmd_cb;
    void *cmd_cb_ctx;

    // Stream integration
    nocti_stream_t *stream;
    bool enable_stream_tx;
    uint32_t stream_read_timeout_ms;

    // UDP packet behavior
    bool use_udp_header;          // slash: true untuk membungkus dengan nocti_udp_hdr_v1
    bool reset_session_on_start;  // slash: true untuk session_id++ pada START

    // Task configs (ESP-IDF stack depth is specified in bytes)
    uint16_t ctrl_task_stack_words;
    UBaseType_t ctrl_task_prio;

    uint16_t adv_task_stack_words;
    UBaseType_t adv_task_prio;

    uint16_t tx_task_stack_words;
    UBaseType_t tx_task_prio;

} nocti_wifi_cfg_t;

struct nocti_wifi_t {
    bool inited;

    volatile bool ap_started;
    volatile bool sta_connected;
    uint32_t sta_ip4;       // last DHCP-assigned station IPv4, host order

    volatile bool has_target;
    nocti_wifi_peer_t target;

    volatile bool streaming_enabled;

    // UDP stream sequencing
    uint32_t session_id;
    uint32_t stream_seq;

    nocti_wifi_cfg_t cfg;

    void *ap_netif;   // slash: esp_netif_t
    void *ev;         // slash: EventGroupHandle_t
    void *mutex;      // slash: SemaphoreHandle_t

    int sock_ctrl;
    int sock_tx;

    void *ctrl_task;
    void *adv_task;
    void *tx_task;

    volatile bool stop_req;
};

void nocti_wifi_cfg_set_defaults(nocti_wifi_cfg_t *cfg);

esp_err_t nocti_wifi_init(nocti_wifi_t *w, const nocti_wifi_cfg_t *cfg);
esp_err_t nocti_wifi_stop(nocti_wifi_t *w);

bool nocti_wifi_is_sta_connected(const nocti_wifi_t *w);
bool nocti_wifi_has_target(const nocti_wifi_t *w);
bool nocti_wifi_is_streaming(const nocti_wifi_t *w);

uint32_t nocti_wifi_session_id(const nocti_wifi_t *w);
uint32_t nocti_wifi_stream_seq(const nocti_wifi_t *w);

esp_err_t nocti_wifi_send_ctrl_to(nocti_wifi_t *w,
                                 const nocti_wifi_peer_t *to,
                                 const void *data,
                                 size_t len);

#ifdef __cplusplus
}
#endif