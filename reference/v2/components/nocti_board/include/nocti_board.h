// ============================================================
// File: components/nocti_board/include/nocti_board.h
// ============================================================
//
// Tujuan file ini
// - Menjadi "single source of truth" untuk pin mapping dan bring-up NoctiNIRS board
// - Board yang memiliki ownership untuk HAL (I2C dan SPI), sehingga modul lain tidak hardcode GPIO
//
// Catatan desain
// - Tipe i2c_port_t dan spi_host_device_t wajib ter-include eksplisit
// - Builder config hanya mengisi default cfg berbasis pin, tidak memulai driver
//

#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h" 
#include "driver/spi_common.h" // slash: untuk spi_host_device_t

#include "nocti_hal_i2c.h"
#include "nocti_hal_spi.h"
#include "nocti_fnirs.h"       // slash: builder fnirs butuh nocti_fnirs_cfg_t

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Identitas board
// ============================================================

#define NOCTI_BOARD_NAME   "Noctic_v1"

// ============================================================
// Pin map NoctiNIRS board Noctic_v1
// slash: pin mapping disentralisasi di board supaya modul lain tidak hardcode GPIO
// ============================================================

typedef struct {
    // I2C
    int i2c_sda; // default GPIO15
    int i2c_scl; // default GPIO16

    // ADS1299 SPI bus and control
    int ads1299_drdy;   // default GPIO10 (DRDY#)
    int ads1299_miso;   // default GPIO11
    int ads1299_sclk;   // default GPIO12
    int ads1299_cs;     // default GPIO13
    int ads1299_start;  // default GPIO14
    int ads1299_reset;  // default GPIO21
    int ads1299_pwdn;   // default GPIO47
    int ads1299_mosi;   // default GPIO48

    // ADS131M04 SPI bus and control
    int ads131_sync_reset; // default GPIO6 (SYNC#/RESET# digabung, pull up 10K ke 3.3V)
    int ads131_cs;         // default GPIO7 (pull up 10K ke 3.3V)
    int ads131_drdy;       // default GPIO35
    int ads131_sclk;       // default GPIO36
    int ads131_miso;       // default GPIO37
    int ads131_mosi;       // default GPIO38

    // Charger enable BQ24295
    int bq_ce; // default GPIO8 (CE pin BQ24295, default active low di datasheet)

    // PCA9956B reset
    int pca_reset; // default GPIO18 (aktif low)

    // USB native
    int usb_dp; // default GPIO20
    int usb_dm; // default GPIO19
} nocti_board_pins_t;

// ============================================================
// I2C addresses
// slash: gunakan 7 bit address untuk driver ESP IDF
// ============================================================

typedef struct {
    uint8_t pca9956b_7bit;   // default 0x01 (AD0 AD1 AD2 semua GND)
    uint8_t max17048_7bit;   // default 0x36 (alamat tipikal MAX17048 fuel gauge)
    uint8_t bq24295_7bit;    // default 0x6B (alamat datasheet BQ24295)
} nocti_board_i2c_addrs_t;

// ============================================================
// SPI policy (flexible)
// slash: board tidak mengunci clock dan mode, bisa reconfig saat idle
// ============================================================

typedef struct {
    uint32_t clock_hz;       // 0 berarti gunakan fallback board
    uint8_t  spi_mode;       // 0 sampai 3, 0xFF berarti gunakan fallback board
    bool     use_dma;        // default false
    int      input_delay_ns; // -1 berarti default 0 di HAL
    uint8_t  queue_size;     // 0 berarti fallback 1
} nocti_board_spi_policy_t;

// ============================================================
// Board config
// slash: host SPI tidak diasumsikan, wajib diisi eksplisit oleh pemanggil
// ============================================================

typedef struct {
    // I2C
    i2c_port_t i2c_port;              // I2C_NUM_0 atau I2C_NUM_1
    uint32_t   i2c_clk_hz;            // contoh 400000 atau 100000
    uint32_t   i2c_trans_queue_depth;  // 0=blocking; >0 enables async queue
    bool       i2c_enable_internal_pullups; // disarankan false kalau ada pullup eksternal

    // SPI host selection
    spi_host_device_t spi_host_ads1299; // wajib diisi
    spi_host_device_t spi_host_ads131;  // wajib diisi

    // Initial SPI policies (boleh diubah nanti via reconfig)
    nocti_board_spi_policy_t spi_ads1299;
    nocti_board_spi_policy_t spi_ads131;

    // Power path options
    bool charger_ce_active_high;
    // slash: false  berarti CE active low (seperti BQ24295 datasheet, enable charge jika CE low)
    // slash: true   berarti CE active high (dipakai jika di hardware ada inverter)

    // Addresses
    nocti_board_i2c_addrs_t i2c_addrs;
} nocti_board_cfg_t;

// ============================================================
// Board runtime handle
// slash: board memiliki ownership untuk HAL handle
// ============================================================

typedef struct {
    bool inited;

    nocti_board_pins_t pins;
    nocti_board_cfg_t  cfg;

    // HAL handles owned by board
    nocti_hal_i2c_t i2c;
    nocti_hal_spi_t spi_ads1299;
    nocti_hal_spi_t spi_ads131;
} nocti_board_t;

// ============================================================
// API
// ============================================================

// slash: helper agar cfg punya nilai aman sebelum init
void      nocti_board_cfg_set_defaults(nocti_board_cfg_t *cfg);

// Bring up dasar: GPIO + ISR + I2C + SPI
esp_err_t nocti_board_init(nocti_board_t *b, const nocti_board_cfg_t *cfg);
esp_err_t nocti_board_deinit(nocti_board_t *b);

// Bring up lengkap termasuk power path BQ24295
// slash: panggil ini dari app_main supaya
// slash: 1) board_init
// slash: 2) disable watchdog BQ24295
// slash: 3) enable charger via CE sesuai polarity cfg
esp_err_t nocti_board_init_all(nocti_board_t *b, nocti_board_cfg_t *cfg);

// Getters
const nocti_board_pins_t      *nocti_board_pins(const nocti_board_t *b);
const nocti_board_i2c_addrs_t *nocti_board_i2c_addrs(const nocti_board_t *b);
const nocti_board_cfg_t       *nocti_board_get_cfg(const nocti_board_t *b);

nocti_hal_i2c_t *nocti_board_i2c(nocti_board_t *b);
nocti_hal_spi_t *nocti_board_spi_ads1299(nocti_board_t *b);
nocti_hal_spi_t *nocti_board_spi_ads131(nocti_board_t *b);

// Board level controls
esp_err_t nocti_board_set_charger_enable(nocti_board_t *b, bool enable);

// SPI reconfig
// slash: panggil hanya saat idle, tidak ada transaksi SPI yang berjalan
// slash: reconfig dilakukan dengan deinit lalu init ulang device handle
esp_err_t nocti_board_spi_reconfig_ads1299(nocti_board_t *b, const nocti_board_spi_policy_t *p);
esp_err_t nocti_board_spi_reconfig_ads131(nocti_board_t *b, const nocti_board_spi_policy_t *p);

// Optional debug
esp_err_t nocti_board_i2c_probe_all(nocti_board_t *b, uint32_t timeout_ms);

// Debug khusus charger BQ24295
// slash: baca register 0x00 sampai 0x09 dan print ke log
esp_err_t nocti_board_bq24295_dump(nocti_board_t *b);

// ============================================================
// Builder config (preset pabrik)
// slash: builder hanya mengisi cfg default berbasis pin board, tidak start driver
// ============================================================

esp_err_t nocti_board_build_fnirs_cfg(const nocti_board_t *b,
                                      nocti_fnirs_cfg_t *out_cfg,
                                      nocti_hal_spi_t *spi_ads131);

#ifdef __cplusplus
}
#endif
