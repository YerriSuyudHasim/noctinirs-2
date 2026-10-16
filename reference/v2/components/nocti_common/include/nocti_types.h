// ============================================================
// File: components/nocti_common/include/nocti_types.h
// ============================================================
//
// Tujuan file ini
// - Menjadi "bahasa dasar" NoctiNIRS: semua tipe data inti ditetapkan di sini
// - Dipakai lintas modul (fnirs, biopot, ble, storage) agar format konsisten
//
// Aturan desain
// - Tidak bergantung ke driver ESP-IDF
// - Hanya pakai tipe standar C (stdint, stdbool)
// - Semua timestamp dan counter pakai integer agar deterministik

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// Waktu dan counter global
// ------------------------------------------------------------

// Timestamp mikrodetik
// - t_boot_us: monoton sejak boot (tidak reset)
// - t_rec_us : sejak START recording (reset ke 0 setiap sesi)
typedef int64_t  nocti_time_us_t;      // slash: signed supaya aman untuk selisih waktu (delta) yang bisa negatif
typedef uint32_t nocti_session_id_t;   // slash: id sesi recording, naik setiap START
typedef uint32_t nocti_pkt_seq_t;      // slash: urutan packet global, naik setiap packet terkirim
typedef uint32_t nocti_sample_id_t;    // slash: urutan sample per modality (fnirs, biopot, dsb)

// ------------------------------------------------------------
// Unit data sains utama
// ------------------------------------------------------------

// fNIRS Optical Density (OD)
// Kita pakai fixed point agar hemat bandwidth dan konsisten antar platform
// od_u1e6 = OD * 1,000,000
typedef int32_t nocti_od_u1e6_t;       // slash: OD bisa negatif setelah detrend atau koreksi, jadi signed

// Biopotential microvolt
// uV = mikrovolt (1e-6 V)
typedef int32_t nocti_uV_t;            // slash: sinyal EEG, EOG, EMG bisa positif dan negatif

// ------------------------------------------------------------
// Kanal dan label sederhana
// ------------------------------------------------------------

// Wavelength fNIRS, sesuai sistem kamu: 735 dan 850 nm
typedef enum {
    NOCTI_WL_735 = 0,
    NOCTI_WL_850 = 1,
} nocti_wavelength_t;

// Modality id untuk menandai sumber data
typedef enum {
    NOCTI_MOD_FNIRS  = 0,             // slash: fNIRS OD samples
    NOCTI_MOD_BIOPOT = 1,             // slash: ADS1299 uV samples
    NOCTI_MOD_STATUS = 2,             // slash: status seperti battery, suhu, flag
    NOCTI_MOD_EVENT  = 3,             // slash: event log (disconnect, overflow, dll)
} nocti_modality_t;

// ------------------------------------------------------------
// Skala dan helper fixed point
// ------------------------------------------------------------

// Konstanta skala OD
#define NOCTI_OD_SCALE_U1E6  (1000000) // slash: OD * 1e6 disimpan sebagai int32

// Convert float ke fixed point OD, hanya dipakai kalau memang ada float di modul tertentu
// Catatan: ini macro sederhana, pastikan input sudah dalam range yang aman
#define NOCTI_OD_TO_U1E6(od_float) ((nocti_od_u1e6_t)((od_float) * (double)NOCTI_OD_SCALE_U1E6))

// Convert fixed point OD ke double
#define NOCTI_OD_FROM_U1E6(od_u1e6) ((double)(od_u1e6) / (double)NOCTI_OD_SCALE_U1E6)

// ------------------------------------------------------------
// Batas aman umum (opsional, bisa dipakai modul lain)
// ------------------------------------------------------------

// Untuk menghindari magic number di banyak tempat
#define NOCTI_MAX_FNIRS_PD_CH        (4)   // slash: ADS131M04 bisa mapping PD CH0-CH3
#define NOCTI_MAX_FNIRS_SOURCES      (10)  // slash: 10 sumber fisik yang kamu pakai
#define NOCTI_MAX_FNIRS_WAVELENGTHS  (2)   // slash: 735 dan 850

// ============================================================
// Stream payload structs (v1)
// // belajar: stream hanya membungkus payload, jadi format payload harus stabil
// // belajar: semua struct di bawah ini dipakai sebagai payload untuk nocti_stream_push
//
// Konvensi endian
// - ESP32 adalah little endian
// - Payload struct ini dikirim apa adanya (binary)
// - Decoder di PC harus parse sebagai little endian
// ============================================================

// ------------------------------------------------------------
// Payload: fNIRS record v1 (NOCTI_REC_FNIRS)
// ------------------------------------------------------------

#define NOCTI_PAYLOAD_FNIRS_V1_VERSION      (1u)

typedef enum {
    NOCTI_FNIRS_F_NONE          = 0x00,
    NOCTI_FNIRS_F_UNDER_K       = 0x01, // slash: n_used < k_fixed
    NOCTI_FNIRS_F_CRC_DROPPED   = 0x02, // slash: ada sample dalam window yang drop karena CRC
} nocti_fnirs_flags_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;        // = NOCTI_PAYLOAD_FNIRS_V1_VERSION
    uint8_t  flags;          // nocti_fnirs_flags_t

    uint8_t  led_id;         // 0..NOCTI_MAX_FNIRS_SOURCES-1
    uint8_t  wavelength;     // nocti_wavelength_t (0=735, 1=850)

    uint8_t  current_mA;     // 0..57 (sesuai clamp driver)
    uint8_t  n_used;         // 0..k_fixed
    uint8_t  k_fixed;        // contoh 3
    uint8_t  _rsv0;          // slash: padding untuk future use

    uint16_t settle_us;      // settle yang dipakai slot
    uint16_t window_us;      // panjang window averaging

    int32_t  avg[4];         // avg raw ADC counts CH0..CH3
} nocti_payload_fnirs_v1_t;

// slash: compile time check untuk jaga kompatibilitas decoder
typedef char nocti_payload_fnirs_v1_size_check[(sizeof(nocti_payload_fnirs_v1_t) == 28) ? 1 : -1];

// ------------------------------------------------------------
// Payload: status record v1 (NOCTI_REC_STATUS)
// ------------------------------------------------------------

#define NOCTI_PAYLOAD_STATUS_V1_VERSION     (1u)

typedef struct __attribute__((packed)) {
    uint8_t  version;        // = NOCTI_PAYLOAD_STATUS_V1_VERSION
    uint8_t  flags;          // reserved
    uint16_t battery_mv;     // contoh: 3700
    int16_t  temp_c_x100;    // contoh: 2534 berarti 25.34 C
    uint16_t vled_mv;        // optional rail monitor
    uint16_t vcore_mv;       // optional rail monitor
} nocti_payload_status_v1_t;

// ------------------------------------------------------------
// Payload: event record v1 (NOCTI_REC_EVENT)
// ------------------------------------------------------------

#define NOCTI_PAYLOAD_EVENT_V1_VERSION      (1u)

typedef struct __attribute__((packed)) {
    uint8_t  version;        // = NOCTI_PAYLOAD_EVENT_V1_VERSION
    uint8_t  level;          // 0=info, 1=warn, 2=err
    uint16_t code;           // event code
    uint32_t arg;            // event arg (counter, id, dsb)
} nocti_payload_event_v1_t;

// ============================================================
// Payload: fNIRS config v1 (NOCTI_REC_FNIRS_CFG)
// slash:
// /belajar: config dikirim sekali saat mulai stream, agar slot record tidak perlu ulang ulang konstanta
// ============================================================

#define NOCTI_PAYLOAD_FNIRS_CFG_V1_VERSION      (1u)

typedef enum {
    NOCTI_FNIRS_MAP_LEDID2_WL = 0,  // slash: slot_index = led_id*2 + wl(735=0,850=1)
} nocti_fnirs_map_mode_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;          // = NOCTI_PAYLOAD_FNIRS_CFG_V1_VERSION
    uint8_t  map_mode;         // nocti_fnirs_map_mode_t

    uint8_t  frame_fs_hz;      // contoh 10
    uint8_t  slot_count;       // contoh 20

    uint8_t  k_fixed;          // contoh 3
    uint8_t  current_735_mA;   // contoh 10
    uint8_t  current_850_mA;   // contoh 10
    uint8_t  _rsv0;

    uint16_t settle_us;        // contoh 200
    uint16_t window_us;        // contoh 3200
} nocti_payload_fnirs_cfg_v1_t;

typedef char nocti_payload_fnirs_cfg_v1_size_check[(sizeof(nocti_payload_fnirs_cfg_v1_t) == 12) ? 1 : -1];

// Paired LED-ON / LED-OFF acquisition metadata. The two phase records use the
// same frame_id and are paired by the host. Both phases use the same fixed K so
// subtraction never mixes averages with different sample counts.
#define NOCTI_PAYLOAD_FNIRS_CFG_V2_VERSION      (2u)

typedef struct __attribute__((packed)) {
    uint8_t  version;          // = NOCTI_PAYLOAD_FNIRS_CFG_V2_VERSION
    uint8_t  map_mode;         // nocti_fnirs_map_mode_t
    uint8_t  frame_fs_hz;
    uint8_t  slot_count;

    uint8_t  k_on;
    uint8_t  k_off;
    uint8_t  current_735_mA;
    uint8_t  current_850_mA;

    uint16_t on_settle_us;
    uint16_t on_window_us;
    uint16_t off_min_settle_us;
    uint16_t slot_period_us;
} nocti_payload_fnirs_cfg_v2_t;

typedef char nocti_payload_fnirs_cfg_v2_size_check[(sizeof(nocti_payload_fnirs_cfg_v2_t) == 16) ? 1 : -1];


// ============================================================
// Payload: fNIRS frame v1/v2 (NOCTI_REC_FNIRS_FRAME)
// slash:
// /belajar: 1 payload mewakili 1 frame (100 ms) berisi 20 slot
// /belajar: mapping slot tidak dikirim, karena sudah dijelaskan lewat FNIRS_CFG
// ============================================================

#define NOCTI_PAYLOAD_FNIRS_FRAME_V1_VERSION    (1u)
#define NOCTI_PAYLOAD_FNIRS_FRAME_V2_VERSION    (2u)

typedef enum {
    NOCTI_FNIRS_FRAME_F_NONE       = 0x00,
    NOCTI_FNIRS_FRAME_F_PARTIAL    = 0x01, // one or more slots are incomplete
    NOCTI_FNIRS_FRAME_F_LED_OFF    = 0x02, // v2 phase record: 0=LED-ON, 1=LED-OFF
    NOCTI_FNIRS_FRAME_F_PAIRED     = 0x04, // v2 record belongs to a paired ON/OFF frame
} nocti_fnirs_frame_flags_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;        // = V1 for legacy, V2 for paired phase records
    uint8_t  flags;          // nocti_fnirs_frame_flags_t

    uint16_t frame_id;       // slash: monotonik, wrap ok

    uint32_t valid_mask;     // slash: bit i = slot i punya data (n_used > 0), pakai 20 bit terbawah
    uint32_t underk_mask;    // slash: bit i = slot i n_used < k_fixed
    uint32_t crc_mask;       // slash: bit i = slot i pernah drop sample karena CRC di window

    int32_t  avg[20][4];     // slash: [slot][ch], raw ADC counts CH0..CH3
    uint8_t  n_used[20];     // slash: 0..k_fixed, untuk debug kualitas window
} nocti_payload_fnirs_frame_v1_t;

// slash: pastikan muat dalam NOCTI_STREAM_MAX_PAYLOAD_BYTES (512)
typedef char nocti_payload_fnirs_frame_v1_size_check[(sizeof(nocti_payload_fnirs_frame_v1_t) <= 512) ? 1 : -1];


// ============================================================
// Payload: biopot config v1 (NOCTI_REC_BIOPOT_CFG)
// slash:
// /belajar: config dikirim sekali saat mulai stream, biar decoder tahu fs, gain, dan mask channel
// ============================================================

#define NOCTI_PAYLOAD_BIOPOT_CFG_V1_VERSION      (1u)

typedef enum {
    NOCTI_BIOPOT_FMT_RAW_COUNTS = 0, // slash: raw ADC code 24-bit yang sudah sign-extend ke int32
    NOCTI_BIOPOT_FMT_UV         = 1, // slash: microvolt (opsional, biasanya tidak dipakai di MCU)
} nocti_biopot_fmt_t;

typedef enum {
    NOCTI_BIOPOT_CFG_F_NONE      = 0x00,
    NOCTI_BIOPOT_CFG_F_BIAS_EN   = 0x01, // slash: BIAS buffer aktif, BIASOUT terhubung ke electrode
} nocti_biopot_cfg_flags_t;

typedef struct __attribute__((packed)) {
    // byte 0
    uint8_t  version;          // = NOCTI_PAYLOAD_BIOPOT_CFG_V1_VERSION
    // bytes 1-2: uint16_t untuk support fs > 255 (misal 1000 SPS)
    // /belajar: sebelumnya uint8_t, overflow di 1000 SPS (1000 & 0xFF = 232)
    uint16_t fs_hz;            // contoh: 250 atau 1000 (SPS)
    // byte 3
    uint8_t  n_ch;             // slash: 8 untuk ADS1299
    // byte 4
    uint8_t  fmt;              // nocti_biopot_fmt_t
    // byte 5
    uint8_t  ch_enable_mask;   // slash: bit0..bit7 channel enable
    // byte 6
    uint8_t  bias_sensp_mask;  // slash: bit0..bit7 channel masuk BIAS_SENSP
    // byte 7
    uint8_t  bias_sensn_mask;  // slash: bit0..bit7 channel masuk BIAS_SENSN
    // byte 8
    uint8_t  flags;            // nocti_biopot_cfg_flags_t
    // bytes 9-10
    uint16_t vref_mV;          // contoh: 4500
    // bytes 11-18
    uint8_t  gain[8];          // slash: gain per channel (1,2,4,6,8,12,24), index 0=CH1..7=CH8
    // byte 19: kurangi satu dari _rsv0[2] agar total tetap 20 bytes
    uint8_t  _rsv0[1];         // slash: future use
} nocti_payload_biopot_cfg_v1_t;

// slash: compile time check untuk jaga kompatibilitas decoder
// Layout: ver(1) + fs_hz(2) + n_ch(1) + fmt(1) + masks(3) + flags(1) + vref(2) + gain(8) + rsv(1) = 20
typedef char nocti_payload_biopot_cfg_v1_size_check[(sizeof(nocti_payload_biopot_cfg_v1_t) == 20) ? 1 : -1];


// ============================================================
// Payload: biopot frame v1 (NOCTI_REC_BIOPOT_FRAME)
// slash:
// /belajar: batching beberapa sample biopot per payload menurunkan overhead UDP dan mutex stream
// /belajar: status24 disimpan di 24-bit LSB, upper 8-bit nol
// ============================================================

#define NOCTI_PAYLOAD_BIOPOT_FRAME_V1_VERSION    (1u)
#define NOCTI_BIOPOT_FRAME_MAX_SAMPLES           (10u)

typedef enum {
    NOCTI_BIOPOT_FRAME_F_NONE     = 0x00,
    NOCTI_BIOPOT_FRAME_F_PARTIAL  = 0x01, // slash: frame di-flush sebelum penuh (n_samples < max)
    NOCTI_BIOPOT_FRAME_F_DROPPED  = 0x02, // slash: ada sample drop karena queue atau ringbuf penuh
} nocti_biopot_frame_flags_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;          // = NOCTI_PAYLOAD_BIOPOT_FRAME_V1_VERSION
    uint8_t  flags;            // nocti_biopot_frame_flags_t
    uint16_t frame_id;         // slash: monotonik, wrap ok

    uint8_t  n_samples;        // 1..NOCTI_BIOPOT_FRAME_MAX_SAMPLES
    uint8_t  ch_enable_mask;   // slash: duplikasi dari cfg untuk robustness decoder
    uint16_t _rsv0;            // slash: align 32-bit

    uint32_t status24[NOCTI_BIOPOT_FRAME_MAX_SAMPLES];
    int32_t  ch[NOCTI_BIOPOT_FRAME_MAX_SAMPLES][8];   // slash: sign-extend 24-bit ke int32
} nocti_payload_biopot_frame_v1_t;

// slash: pastikan muat dalam NOCTI_STREAM_MAX_PAYLOAD_BYTES (512)
typedef char nocti_payload_biopot_frame_v1_size_check[(sizeof(nocti_payload_biopot_frame_v1_t) <= 512) ? 1 : -1];

#ifdef __cplusplus
}
#endif
