// ============================================================
// File: components/nocti_ble/include/nocti_ble.h
// ============================================================

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// forward decl supaya header ini ringan
typedef struct nocti_stream_t nocti_stream_t;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// NoctiNIRS BLE (NimBLE, ESP-IDF v5.5.x)
// // belajar: module ini fokus ke BLE transport (GATT, advertising, notify)
// // belajar: producer data tetap nocti_stream, BLE hanya consumer + command RX
// ============================================================

// batas aman command dari central ke device
#define NOCTI_BLE_MAX_CMD_BYTES (256u)

// header fragment BLE untuk mengirim 1 stream frame yang ter-fragment
// total 8 byte supaya ringan
typedef struct __attribute__((packed)) {
    uint16_t frame_seq;   // slash: ID frame untuk grouping fragment (biasanya ambil dari nocti_stream_hdr.seq)
    uint16_t frame_len;   // slash: total panjang frame asli (byte)
    uint8_t  frag_idx;    // slash: index fragment [0..frag_cnt-1]
    uint8_t  frag_cnt;    // slash: total fragment untuk frame ini
    uint16_t frag_off;    // slash: offset data fragment di frame asli
} nocti_ble_frag_hdr_t;

// callback saat ada command dari central (WRITE ke characteristic CMD)
typedef void (*nocti_ble_cmd_cb_t)(const uint8_t *data, size_t len, void *user_ctx);

// callback event koneksi
typedef void (*nocti_ble_conn_cb_t)(bool connected, uint16_t conn_handle, void *user_ctx);

typedef struct {
    // basic identity
    const char *device_name;        // slash: nama BLE di iklan (advertising)
    uint16_t preferred_mtu;         // slash: request MTU via ATT exchange (misal 247 atau 256)

    // advertising interval (unit 0.625 ms sesuai BLE spec)
    uint16_t adv_itvl_min;          // slash: contoh 160 => 100 ms
    uint16_t adv_itvl_max;          // slash: contoh 240 => 150 ms
    bool adv_include_name;          // slash: taruh nama di advertising packet

    // command RX callback
    nocti_ble_cmd_cb_t  cmd_cb;     // slash: dipanggil saat ada WRITE pada CMD char
    void               *cmd_cb_ctx;

    // connection callback
    nocti_ble_conn_cb_t conn_cb;    // slash: inform connect / disconnect
    void               *conn_cb_ctx;

    // streaming from nocti_stream via notify
    nocti_stream_t *stream;              // slash: kalau NULL, BLE tidak auto kirim stream
    uint32_t stream_read_timeout_ms;     // slash: timeout read dari ringbuffer stream
    bool enable_stream_tx;              // slash: create task TX jika true dan stream non-NULL
    uint16_t tx_task_stack_words;        // slash: stack task TX (word, bukan byte)
    UBaseType_t tx_task_prio;            // slash: priority task TX
} nocti_ble_cfg_t;

typedef struct {
    bool inited;
    bool connected;
    bool notify_enabled;      // slash: central sudah enable notify untuk DATA char

    uint8_t  addr_type;       // slash: BLE address type yang dipilih stack
    uint16_t conn_handle;     // slash: handle koneksi aktif (valid kalau connected)

    // runtime config
    nocti_ble_cfg_t cfg;

    // internal scratch untuk command
    uint8_t cmd_buf[NOCTI_BLE_MAX_CMD_BYTES];

    // task handle untuk TX stream
    TaskHandle_t tx_task;
} nocti_ble_t;

// ============================================================
// API
// ============================================================

esp_err_t nocti_ble_init(nocti_ble_t *b, const nocti_ble_cfg_t *cfg);
esp_err_t nocti_ble_start_advertising(nocti_ble_t *b);
esp_err_t nocti_ble_stop(nocti_ble_t *b);

// kirim 1 stream frame mentah (akan di-fragment sesuai MTU)
esp_err_t nocti_ble_send_stream_frame(nocti_ble_t *b, const uint8_t *frame, size_t frame_len);

// helper state
bool     nocti_ble_is_connected(const nocti_ble_t *b);
uint16_t nocti_ble_conn_handle(const nocti_ble_t *b);
bool nocti_ble_can_tx_stream(const nocti_ble_t *b);


#ifdef __cplusplus
}
#endif
