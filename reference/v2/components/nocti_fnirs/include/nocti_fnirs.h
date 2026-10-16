// ============================================================
// File: components/nocti_fnirs/include/nocti_fnirs.h
// ADS131M04 fNIRS front end driver (NoctiNIRS)
// ============================================================

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "nocti_types.h"     // slash: nocti_time_us_t, nocti_wavelength_t
#include "nocti_hal_spi.h"   // slash: nocti_hal_spi_t

// slash: fallback define kalau project belum define
#ifndef NOCTI_MAX_FNIRS_SOURCES
#define NOCTI_MAX_FNIRS_SOURCES 10u
#endif

#ifndef NOCTI_MAX_FNIRS_WAVELENGTHS
#define NOCTI_MAX_FNIRS_WAVELENGTHS 2u
#endif

#ifndef NOCTI_FNIRS_SLOTS_PER_FRAME
#define NOCTI_FNIRS_SLOTS_PER_FRAME (NOCTI_MAX_FNIRS_SOURCES * NOCTI_MAX_FNIRS_WAVELENGTHS)
#endif

// slash: helper konversi signed 24-bit (sign-extended) ke raw u24 (0..0xFFFFFF)
static inline uint32_t nocti_fnirs_s24_to_u24(int32_t s24)
{
    // slash: jika negatif, tambahkan 2^24 untuk balik ke representasi raw
    if (s24 < 0) return (uint32_t)(s24 + (int32_t)(1 << 24));
    return (uint32_t)s24;
}

// slash: record 1 sample hasil 1 kali baca ADS131M04 (CH0..CH3)
typedef struct {
    nocti_time_us_t timestamp_us;   // timestamp captured at the DRDY falling edge
    uint32_t drdy_sequence;         // monotonically increasing DRDY edge count

    uint8_t slot_index;             // slash: 0..19, mapping led_id dan wavelength
    uint8_t led_id;                 // slash: 0..9
    uint8_t wavelength;             // slash: NOCTI_WL_735 atau NOCTI_WL_850 (cast to u8)
    uint8_t current_mA;             // slash: metadata arus LED (boleh 0 kalau dark read)

    uint32_t status24;              // slash: STATUS word 24-bit (MSB aligned status16 di bit 23..8)

    // slash: dua view sekaligus biar debug enak
    int32_t  ch[4];                 // slash: CH0..CH3 signed (24-bit sign-extended)
    uint32_t ch_u24[4];             // slash: CH0..CH3 raw u24 (0..0xFFFFFF)

    uint32_t crc24;                 // slash: CRC word 24-bit (umumnya CRC16 MSB aligned)
    uint16_t crc16_rx;              // slash: CRC16 dari device (extract dari crc24)
    uint16_t crc16_calc;            // slash: CRC16 hasil hitung lokal
    bool     crc_ok;                // slash: true jika rx == calc
} nocti_fnirs_sample_t;

// slash: frame mentah ADS131M04 (tanpa metadata LED)
// /belajar: ini dipakai untuk mode "producer" yang baca ADC terus menerus
// /belajar: scheduler cukup memilih sampel yang timestamp-nya jatuh di window LED ON
typedef struct {
    nocti_time_us_t timestamp_us;   // timestamp captured at the DRDY falling edge
    uint32_t drdy_sequence;         // monotonically increasing DRDY edge count

    uint32_t status24;              // slash: STATUS word 24-bit
    int32_t  ch[4];                 // slash: CH0..CH3 signed (24-bit sign-extended)
    uint32_t ch_u24[4];             // slash: CH0..CH3 raw u24

    uint32_t crc24;                 // slash: CRC word 24-bit
    uint16_t crc16_rx;              // slash: CRC16 dari device
    uint16_t crc16_calc;            // slash: CRC16 hasil hitung lokal
    bool     crc_ok;                // slash: true jika rx == calc
} nocti_fnirs_frame_t;

// Runtime diagnostics for the DRDY event engine. Every valid frame read is
// paired with one event that was captured at the GPIO falling edge.
typedef struct {
    bool capture_enabled;
    int8_t isr_core_id;
    int8_t consumer_core_id;

    uint32_t irq_count;
    uint32_t event_push_count;
    uint32_t event_pop_count;
    uint32_t event_overflow_count;
    uint32_t event_depth_max;
    uint32_t fifo_risk_count;       // software backlog exceeded ADS131 FIFO depth=2
    uint32_t timing_fault_count;    // IRQ/timestamp continuity was not one period
    uint32_t spi_recovery_count;    // SPI transaction failed; FIFO epoch was reset
    uint32_t recovery_count;        // all bounded SYNC recoveries during acquisition
    uint32_t wait_timeout_count;
    uint32_t capture_start_count;
    uint32_t capture_stop_count;
    uint32_t sync_count;

    uint32_t max_irq_to_spi_start_us;
    uint32_t max_spi_xfer_us;
    uint32_t max_frame_service_us;
} nocti_fnirs_stats_t;

// slash: config init ADS131M04
typedef struct {
    nocti_hal_spi_t *spi;       // slash: handle SPI HAL (sudah init di board)
    int gpio_drdy;              // slash: DRDY pin dari ADS131M04 ke ESP32
    int gpio_sync;              // slash: SYNC pin, boleh sama dengan gpio_reset kalau gabungan
    int gpio_reset;             // slash: RESET pin, boleh sama dengan gpio_sync kalau gabungan

    uint32_t clkin_hz;          // slash: frekuensi CLKIN, default 8192000
    uint32_t drdy_timeout_us;   // slash: timeout tunggu DRDY event
    uint32_t expected_drdy_period_us; // 0 disables driver-level continuity guard
    uint32_t drdy_period_tolerance_us; // allowed timestamp error around one period
    uint32_t spi_timeout_ms;    // slash: timeout HAL SPI

    bool do_hw_reset_pulse;     // slash: toggle SYNC/RESET low cukup lama untuk hardware reset
    bool do_cmd_reset;          // slash: kirim RESET command via SPI (opsional)
    bool do_adc_register_init;  // slash: lakukan UNLOCK dan write register pilihan

    // slash: CRC check untuk buang frame korup
    bool crc_check_enable;      // slash: true = hitung CRC16 dan tandai crc_ok
    uint8_t crc_retry;          // slash: retry baca frame kalau CRC gagal (0..3)

    // slash: optional output queue
    uint16_t queue_len;         // slash: 0 = queue off, >0 buat queue untuk stream async

    // slash: register values, pakai 0 untuk skip write (biar tetap default)
    uint16_t reg_mode;          // slash: MODE addr 0x02
    uint16_t reg_clock;         // slash: CLOCK addr 0x03
    uint16_t reg_gain;          // slash: GAIN addr 0x04
} nocti_fnirs_cfg_t;

// slash: helper default config
void nocti_fnirs_cfg_set_defaults(nocti_fnirs_cfg_t *cfg);

// slash: init dan deinit
esp_err_t nocti_fnirs_init(const nocti_fnirs_cfg_t *cfg);
esp_err_t nocti_fnirs_deinit(void);

// Re-synchronize the ADS131M04 digital filters and clear its two-deep data
// FIFO after a pause in data collection. The pulse is intentionally shorter
// than the reset threshold, so register configuration is retained.
esp_err_t nocti_fnirs_sync_conversions(void);

// Start/stop the continuous DRDY capture engine. Start clears stale software
// events, strobes SYNC/RESET with a non-reset pulse to clear the ADS131M04
// two-deep FIFO, then binds all DRDY wakeups to one permanent consumer task.
esp_err_t nocti_fnirs_acquisition_start(TaskHandle_t consumer_task);
void nocti_fnirs_acquisition_stop(void);
bool nocti_fnirs_acquisition_is_running(void);

// slash: optional output queue (kalau queue_len > 0)
QueueHandle_t nocti_fnirs_get_queue(void);

// slash: baca 1 frame ADS131 (tunggu DRDY), metadata slot diberikan manual
// led_id: 0..9, wl: NOCTI_WL_735/NOCTI_WL_850, current_mA boleh 0 untuk dark read
esp_err_t nocti_fnirs_read_sample(uint8_t led_id,
                                 nocti_wavelength_t wl,
                                 uint8_t current_mA,
                                 nocti_fnirs_sample_t *out_opt);

// slash: baca 1 frame ADS131M04 (tunggu DRDY) tanpa metadata LED
// /belajar: dipakai untuk task producer ADC agar callback scheduler tetap ringan
esp_err_t nocti_fnirs_read_frame(nocti_fnirs_frame_t *out_opt);

// slash: debug helpers untuk verifikasi SPI dan DRDY
uint32_t  nocti_fnirs_dbg_drdy_count(void);
void nocti_fnirs_get_stats(nocti_fnirs_stats_t *out_stats);
void nocti_fnirs_reset_stats(void);
esp_err_t nocti_fnirs_dbg_read_reg_u16(uint8_t addr, uint16_t *out_u16);
esp_err_t nocti_fnirs_dbg_write_reg_u16(uint8_t addr, uint16_t val);
