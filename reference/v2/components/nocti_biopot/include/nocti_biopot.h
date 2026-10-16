// ============================================================
// File: components/nocti_biopot/include/nocti_biopot.h
// Revisi kecil:
// 1) Hapus include yang tidak dipakai di header (nocti_timebase.h)
// 2) Pastikan komentar pin polarity konsisten (PWDN active low)
// 3) Tambahkan default value note yang jelas (opsional)
// ============================================================

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "nocti_types.h"
#include "nocti_hal_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// NoctiNIRS Biopot (ADS1299)
// Fokus: setup ADS1299, akuisisi frame 8 channel, dan utilitas (gain, enable, impedansi).
// Streaming BLE dan framing packet ada di nocti_stream.
// ============================================================

// ============================================================
// slash: catatan integrasi
// /belajar: komponen ini sengaja hanya urus ADS1299 (SPI, register, DRDY)
// /belajar: batching dan UDP framing dilakukan di layer app atau nocti_stream
// ============================================================

// ============================================================
// Konstanta dasar frame ADS1299 (tanpa daisy chain)
// STATUS 3 byte + 8 channel x 3 byte = 27 byte
// ============================================================
#define NOCTI_BIOPOT_NUM_CH          8u
#define NOCTI_BIOPOT_STATUS_BYTES    3u
#define NOCTI_BIOPOT_WORD_BYTES      3u
#define NOCTI_BIOPOT_FRAME_BYTES     (NOCTI_BIOPOT_STATUS_BYTES + (NOCTI_BIOPOT_NUM_CH * NOCTI_BIOPOT_WORD_BYTES))

// ============================================================
// Gain per channel (CHnSET gain field)
// ============================================================
typedef enum {
    NOCTI_BIOPOT_GAIN_1  = 1,
    NOCTI_BIOPOT_GAIN_2  = 2,
    NOCTI_BIOPOT_GAIN_4  = 4,
    NOCTI_BIOPOT_GAIN_6  = 6,
    NOCTI_BIOPOT_GAIN_8  = 8,
    NOCTI_BIOPOT_GAIN_12 = 12,
    NOCTI_BIOPOT_GAIN_24 = 24,
} nocti_biopot_gain_t;

// ============================================================
// MUX per channel (CHnSET MUX field)
// ============================================================
typedef enum {
    NOCTI_BIOPOT_MUX_NORMAL     = 0,  // normal electrode input (INxP dan INxN, atau INxN via SRB2 jika SRB2=1)
    NOCTI_BIOPOT_MUX_SHORTED    = 1,  // input shorted (offset atau noise measurement)
    NOCTI_BIOPOT_MUX_BIASMEAS   = 2,  // dipakai bersama CONFIG3.BIAS_MEAS untuk bias measurement
    NOCTI_BIOPOT_MUX_MVDD       = 3,  // MVDD untuk supply measurement
    NOCTI_BIOPOT_MUX_TEMP       = 4,  // temperature sensor
    NOCTI_BIOPOT_MUX_TESTSIG    = 5,  // internal test signal (butuh CONFIG2 diset)
    NOCTI_BIOPOT_MUX_BIAS_DRP   = 6,  // BIAS_DRP (positive electrode adalah driver)
    NOCTI_BIOPOT_MUX_BIAS_DRN   = 7,  // BIAS_DRN (negative electrode adalah driver)
} nocti_biopot_mux_t;

// ============================================================
// Data rate CONFIG1 DR bits (raw 3 bit)
// ============================================================
typedef struct {
    uint8_t dr_bits;  // 0..7 (raw). Default 0x06 untuk 250 SPS.
} nocti_biopot_rate_t;

// ============================================================
// Konfigurasi per channel
// ============================================================
typedef struct {
    bool enable;
    bool srb2;
    nocti_biopot_mux_t mux;
    nocti_biopot_gain_t gain;
} nocti_biopot_ch_cfg_t;

// ============================================================
// Record sample untuk 1 frame ADS1299
// timestamp_us diambil dekat event DRDY sebelum SPI clock.
// ============================================================
typedef struct {
    nocti_time_us_t timestamp_us;        // prefer: 64-bit monotonic us
    uint32_t status24;
    int32_t  ch[NOCTI_BIOPOT_NUM_CH];
} nocti_biopot_sample_t;

// ============================================================
// Konfigurasi utama driver
// gpio_start reset pwdn opsional, isi -1 jika tidak dipakai.
// Queue internal opsional, jika queue_len = 0 maka tidak dibuat.
// ============================================================
typedef struct {
    // SPI handle untuk ADS1299
    nocti_hal_spi_t *spi;

    // GPIO wajib
    int gpio_drdy;     // DRDY# ADS1299, active low

    // GPIO opsional (isi -1 jika tidak dipakai)
    int gpio_start;    // START pin (opsional, banyak desain hanya pakai CMD START/STOP)
    int gpio_reset;    // RESET pin, active low
    int gpio_pwdn;     // PWDN pin, active low

    // Timeout
    uint32_t drdy_timeout_us; // default 10000 us
    uint32_t spi_timeout_ms;  // default 20 ms

    // Startup delay setelah power up dan ref settle
    uint16_t startup_delay_ms; // default 150 ms

    // Data rate
    nocti_biopot_rate_t rate;

    // CONFIG3: enable internal reference buffer dan bias amplifier
    bool enable_internal_vref; // default true
    bool enable_bias;          // default true
    bool enable_loff_comp;     // default false (kalau tidak dipakai, lebih tenang dimatikan)

    // Default per channel config
    nocti_biopot_ch_cfg_t ch[NOCTI_BIOPOT_NUM_CH];

    // Queue internal untuk sample
    uint16_t queue_len;

    // Vref untuk konversi counts ke volt (untuk impedansi)
    float vref_volts; // default 4.5
} nocti_biopot_cfg_t;

// ============================================================
// slash: helper default config
// /belajar: cara paling aman adalah: memset(cfg,0) lalu panggil fungsi ini
//           setelah itu baru override field yang kamu butuhkan
// ============================================================

void nocti_biopot_cfg_set_defaults(nocti_biopot_cfg_t *cfg);

// ============================================================
// Lead off dan impedansi check
// ============================================================

typedef enum {
    NOCTI_BIOPOT_LOFF_I_6NA  = 0,
    NOCTI_BIOPOT_LOFF_I_24NA = 1,
    NOCTI_BIOPOT_LOFF_I_6UA  = 2,
    NOCTI_BIOPOT_LOFF_I_24UA = 3,
} nocti_biopot_loff_i_t;

typedef enum {
    NOCTI_BIOPOT_LOFF_F_DC      = 0,
    NOCTI_BIOPOT_LOFF_F_AC_7P8  = 1,
    NOCTI_BIOPOT_LOFF_F_AC_31P2 = 2,
} nocti_biopot_loff_f_t;

typedef struct {
    uint8_t loff_sensp_mask;
    uint8_t loff_sensn_mask;

    nocti_biopot_loff_i_t i_mag;
    nocti_biopot_loff_f_t f_sel;

    uint16_t n_samples;       // default 250
    float fs_hz;              // default 250.0

    bool read_loff_status;    // default true
} nocti_biopot_imp_cfg_t;

typedef struct {
    float z_ohm[NOCTI_BIOPOT_NUM_CH];
    uint8_t loff_statp;
    uint8_t loff_statn;
} nocti_biopot_imp_result_t;

// ============================================================
// API lifecycle
// ============================================================

esp_err_t nocti_biopot_init(const nocti_biopot_cfg_t *cfg);
esp_err_t nocti_biopot_deinit(void);

// ============================================================
// Streaming control
// ============================================================

esp_err_t nocti_biopot_start(void);
esp_err_t nocti_biopot_stop(void);
bool      nocti_biopot_is_streaming(void);

// ============================================================
// Sample read
// ============================================================

esp_err_t nocti_biopot_read_sample(nocti_biopot_sample_t *out_opt);

// Queue internal opsional
QueueHandle_t nocti_biopot_get_queue(void);

// ============================================================
// slash: BIAS sense override
// /belajar: default driver auto build BIAS_SENSP dan BIAS_SENSN dari channel enable
// /belajar: kalau kamu ingin paksa mask tertentu (misal hanya ECG), pakai override
//           fungsi ini hanya boleh dipanggil saat idle (belum streaming)
// ============================================================

esp_err_t nocti_biopot_set_bias_sense_override(bool enable, uint8_t sensp_mask, uint8_t sensn_mask);

// ============================================================
// Runtime config (hanya boleh saat idle)
// ============================================================

esp_err_t nocti_biopot_set_channel_enable(uint8_t ch1_to_8, bool enable);
esp_err_t nocti_biopot_set_channel_gain(uint8_t ch1_to_8, nocti_biopot_gain_t gain);
esp_err_t nocti_biopot_apply_channel_cfg(uint8_t ch1_to_8, const nocti_biopot_ch_cfg_t *cfg);

// ============================================================
// Impedance check (hanya boleh saat idle)
// ============================================================

esp_err_t nocti_biopot_impedance_check(const nocti_biopot_imp_cfg_t *cfg,
                                      nocti_biopot_imp_result_t *out_opt);

#ifdef __cplusplus
}
#endif
