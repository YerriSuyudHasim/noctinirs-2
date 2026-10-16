// ============================================================
// File: components/nocti_common/include/nocti_packet.h
// ============================================================
//
// Tujuan file ini
// - Mendefinisikan "envelope" universal untuk semua data NoctiNIRS
// - Dipakai bersama oleh BLE dan storage, supaya replay mudah
//
// Prinsip
// - 1 packet = 1 record (fnirs atau biopot atau status atau event)
// - Timestamp utama pakai t_rec_us (reset tiap START)
// - pkt_seq naik tiap packet untuk deteksi loss

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "nocti_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Magic untuk mengenali packet NoctiNIRS di stream byte
#define NOCTI_PKT_MAGIC  (0x4E4F4354u)   // slash: 'NOCT' dalam ASCII

// Versi format packet, beda dari versi firmware
#define NOCTI_PKT_FMT_VER (1u)           // slash: naik jika struktur header berubah

// ------------------------------------------------------------
// Jenis pesan
// ------------------------------------------------------------

typedef enum {
    NOCTI_MSG_INVALID = 0,

    // Data utama
    NOCTI_MSG_FNIRS_OD = 1,              // slash: payload berisi OD fixed point
    NOCTI_MSG_BIOPOT_UV = 2,             // slash: payload berisi microvolt per channel

    // Status dan info
    NOCTI_MSG_STATUS = 10,               // slash: payload berisi nocti_device_status_t ringkas
    NOCTI_MSG_INFO = 11,                 // slash: payload info device, versi, konfigurasi

    // Control plane
    NOCTI_MSG_CMD = 20,                  // slash: host ke device (START, STOP, dsb)
    NOCTI_MSG_ACK = 21,                  // slash: device ke host sebagai balasan command

    // Event
    NOCTI_MSG_EVENT = 30,                // slash: event seperti disconnect, overflow, fault
} nocti_msg_type_t;

// ------------------------------------------------------------
// Flag header (bitmask)
// ------------------------------------------------------------

typedef enum {
    NOCTI_PF_CRC_PRESENT = (1u << 0),    // slash: crc16 field valid
    NOCTI_PF_ENDIAN_LE   = (1u << 1),    // slash: semua integer little-endian (target kita)
} nocti_pkt_flags_t;

// ------------------------------------------------------------
// Header packet
// ------------------------------------------------------------
// Catatan packing
// - Kita buat header fixed size agar parsing cepat
// - Payload mengikuti setelah header
// - CRC16 opsional ditempatkan di akhir payload (lebih fleksibel)

#pragma pack(push, 1)
typedef struct {
    uint32_t        magic;              // slash: NOCTI_PKT_MAGIC
    uint8_t         fmt_ver;            // slash: NOCTI_PKT_FMT_VER
    uint8_t         msg_type;           // slash: nocti_msg_type_t
    uint16_t        flags;              // slash: nocti_pkt_flags_t

    nocti_session_id_t session_id;      // slash: naik tiap START recording
    nocti_pkt_seq_t    pkt_seq;         // slash: naik tiap packet global
    nocti_time_us_t    t_rec_us;         // slash: timestamp sejak recording start

    uint16_t        payload_len;        // slash: panjang payload dalam byte
    uint16_t        header_len;         // slash: panjang header ini, untuk forward compatibility
} nocti_pkt_hdr_t;
#pragma pack(pop)

// Helper untuk isi header default
static inline void nocti_pkt_hdr_init(nocti_pkt_hdr_t *h)
{
    if (!h) return;
    h->magic = NOCTI_PKT_MAGIC;
    h->fmt_ver = (uint8_t)NOCTI_PKT_FMT_VER;
    h->msg_type = (uint8_t)NOCTI_MSG_INVALID;
    h->flags = (uint16_t)NOCTI_PF_ENDIAN_LE;
    h->session_id = 0;
    h->pkt_seq = 0;
    h->t_rec_us = 0;
    h->payload_len = 0;
    h->header_len = (uint16_t)sizeof(nocti_pkt_hdr_t);
}

// ------------------------------------------------------------
// Payload minimal yang sering dipakai
// ------------------------------------------------------------

// Command id
typedef enum {
    NOCTI_CMD_NOP = 0,                  // slash: no-op untuk test
    NOCTI_CMD_START = 1,                // slash: mulai recording, reset t_rec_us ke 0
    NOCTI_CMD_STOP = 2,                 // slash: stop recording
    NOCTI_CMD_INFO = 3,                 // slash: minta info device
    NOCTI_CMD_PING = 4,                 // slash: cek koneksi
    NOCTI_CMD_SET_CFG = 5,              // slash: set config ringan (interval status, dsb)
} nocti_cmd_id_t;

// Payload CMD
#pragma pack(push, 1)
typedef struct {
    uint16_t cmd_id;                    // slash: nocti_cmd_id_t
    uint16_t cmd_seq;                   // slash: urutan command dari host, untuk matching ACK
    uint32_t arg0;                      // slash: argumen generik, arti tergantung cmd_id
    uint32_t arg1;                      // slash: argumen generik
} nocti_cmd_t;
#pragma pack(pop)

// Payload ACK
#pragma pack(push, 1)
typedef struct {
    uint16_t cmd_id;                    // slash: command yang dibalas
    uint16_t cmd_seq;                   // slash: harus sama dengan cmd_seq dari host
    int32_t  result;                    // slash: nocti_err_t atau hasil operasi
    uint32_t info0;                     // slash: info tambahan (misal session_id baru)
} nocti_ack_t;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif
