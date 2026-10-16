// ============================================================
// File: components/nocti_hal_spi/include/nocti_hal_spi.h
// ============================================================

#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/spi_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Konfigurasi SPI untuk satu bus + satu device handle
// Pin tidak dihardcode. Nanti nocti_board yang isi pin.
// // belajar: untuk NoctiNIRS, ADS1299 dan ADS131M04 wajib beda host
// // belajar: karena SCLK MOSI MISO-nya beda total, jadi bukan sekadar beda CS
// ============================================================

typedef struct {
    spi_host_device_t host;   // contoh SPI2_HOST atau SPI3_HOST (ESP32-S3)

    // Bus pins
    int gpio_mosi;
    int gpio_miso;
    int gpio_sclk;

    // Device pin
    int gpio_cs;              // GPIO CS, atau -1 kalau CS di-handle manual

    // SPI timing
    uint32_t clock_hz;        // contoh 2000000 sampai 4000000
    uint8_t spi_mode;         // 0..3
    uint8_t queue_size;       // default 1 atau 2

    // DMA
    bool use_dma;             // true untuk DMA, false untuk non DMA

    // Optional tuning
    int input_delay_ns;       // set -1 kalau tidak dipakai

    // Bus lifetime
    bool free_bus_on_deinit;  // true: deinit juga free bus, false: hanya remove device
} nocti_hal_spi_cfg_t;

// ============================================================
// Handle runtime
// ============================================================

typedef struct {
    spi_host_device_t host;
    spi_device_handle_t dev;

    int gpio_cs;
    bool use_dma;
    bool free_bus_on_deinit;

    // // belajar: kalau bus sudah di-init oleh module lain, kita tidak "own" bus itu
    // // belajar: artinya deinit tidak boleh spi_bus_free walaupun cfg minta free
    bool bus_owned;

    SemaphoreHandle_t mutex;  // // belajar: melindungi transaksi supaya tidak overlap
} nocti_hal_spi_t;

// ============================================================
// Init, deinit
// ============================================================

esp_err_t nocti_hal_spi_init(nocti_hal_spi_t *h, const nocti_hal_spi_cfg_t *cfg);
esp_err_t nocti_hal_spi_deinit(nocti_hal_spi_t *h);

// ============================================================
// Lock, unlock
// timeout_ms: 0 berarti tunggu selamanya (stand-alone style)
// ============================================================

esp_err_t nocti_hal_spi_lock(nocti_hal_spi_t *h, uint32_t timeout_ms);
void      nocti_hal_spi_unlock(nocti_hal_spi_t *h);

// ============================================================
// Transfer utama
// Full duplex dalam satu transaksi SPI (idealnya 1 CS)
// Cocok untuk device berbasis frame seperti ADS131M04.
// tx atau rx boleh NULL.
// // belajar: implementasi akan meng-chunk jika len_bytes besar.
// // belajar: kalau chunk terjadi dan CS dikelola driver, CS bisa toggle di tiap chunk.
// // belajar: untuk ADC frame sensitif CS, pakai transfer_contig dan pastikan len_bytes kecil.
// ============================================================

esp_err_t nocti_hal_spi_transfer(nocti_hal_spi_t *h,
                                const void *tx, void *rx,
                                size_t len_bytes,
                                uint32_t timeout_ms);

// ============================================================
// Transfer kontigu (paksa 1 transaksi SPI saja)
// slash: cocok untuk ADC frame yang sensitif terhadap CS toggling.
// note: len_bytes harus <= max_transfer_sz internal.
// note: untuk read-only kontigu, siapkan tx buffer berisi nol (tx tidak boleh NULL).
// ============================================================

esp_err_t nocti_hal_spi_transfer_contig(nocti_hal_spi_t *h,
                                       const void *tx, void *rx,
                                       size_t len_bytes,
                                       uint32_t timeout_ms);

// ============================================================
// Helper convenience
// ============================================================

esp_err_t nocti_hal_spi_write(nocti_hal_spi_t *h,
                             const void *tx, size_t len_bytes,
                             uint32_t timeout_ms);

esp_err_t nocti_hal_spi_read(nocti_hal_spi_t *h,
                            void *rx, size_t len_bytes,
                            uint32_t timeout_ms);

// ============================================================
// Write lalu read (dua transaksi)
// Perhatian: ini akan menaikkan CS di antara transaksi tergantung driver.
// Jangan gunakan untuk device yang butuh frame kontigu dalam satu CS.
// Untuk ADS131M04, pakai nocti_hal_spi_transfer dan kirim seluruh frame sekaligus.
// ============================================================

esp_err_t nocti_hal_spi_write_then_read(nocti_hal_spi_t *h,
                                       const void *tx, size_t tx_len,
                                       void *rx, size_t rx_len,
                                       uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
