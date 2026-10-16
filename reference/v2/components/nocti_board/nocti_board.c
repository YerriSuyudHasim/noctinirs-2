// ============================================================
// File: components/nocti_board/nocti_board.c
// ============================================================
//
// Tujuan file ini
// - Bring up board Noctic_v1: init GPIO dasar, install ISR service, init HAL I2C dan SPI
// - Menyediakan getter untuk pin map dan handle HAL
// - Menyediakan reconfig SPI policy saat idle
// - Menyediakan init power path dasar untuk BQ24295 (matikan watchdog, enable charger)
//

#include "nocti_board.h"

#include <string.h>

#include "nocti_log.h"

static const char *TAG = "nocti_board";

// ============================================================
// Internal defaults
// ============================================================

static nocti_board_pins_t default_pins_noctic_v1(void)
{
    nocti_board_pins_t p;
    memset(&p, 0, sizeof(p));

    // I2C
    p.i2c_sda = 15;
    p.i2c_scl = 16;

    // ADS1299
    p.ads1299_drdy  = 10;
    p.ads1299_miso  = 11;
    p.ads1299_sclk  = 12;
    p.ads1299_cs    = 13;
    p.ads1299_start = 14;
    p.ads1299_reset = 21;
    p.ads1299_pwdn  = 47;
    p.ads1299_mosi  = 48;

    // ADS131M04
    p.ads131_sync_reset = 6;
    p.ads131_cs         = 7;
    p.ads131_drdy       = 35;
    p.ads131_sclk       = 36;
    p.ads131_miso       = 37;
    p.ads131_mosi       = 38;

    // Charger enable BQ24295
    p.bq_ce = 8;

    // PCA reset
    p.pca_reset = 18;

    // USB native
    p.usb_dp = 20;
    p.usb_dm = 19;

    return p;
}

static bool host_ok(spi_host_device_t h)
{
    return (h == SPI2_HOST) || (h == SPI3_HOST);
}

static nocti_board_spi_policy_t spi_policy_default(void)
{
    nocti_board_spi_policy_t p;
    memset(&p, 0, sizeof(p));
    p.clock_hz       = 0;      // slash: 0 berarti fallback board
    p.spi_mode       = 0xFFu;  // slash: 0xFF berarti fallback board
    p.use_dma        = false;
    p.input_delay_ns = -1;     // slash: -1 berarti fallback HAL (0 ns)
    p.queue_size     = 0;      // slash: 0 berarti fallback 1
    return p;
}

static uint8_t choose_spi_mode(uint8_t requested, uint8_t fallback)
{
    // slash: mode 0 sampai 3 valid, sentinel "unset" adalah 0xFF
    return (requested == 0xFFu) ? fallback : requested;
}

static uint32_t choose_spi_clock(uint32_t requested, uint32_t fallback)
{
    // slash: 0 berarti "unset"
    return (requested == 0u) ? fallback : requested;
}

static uint8_t choose_queue(uint8_t requested, uint8_t fallback)
{
    // slash: 0 berarti "unset"
    return (requested == 0u) ? fallback : requested;
}

// ============================================================
// GPIO init
// ============================================================

static esp_err_t init_gpios(const nocti_board_pins_t *p)
{
    if (!p) return ESP_ERR_INVALID_ARG;

    // slash: set hanya pin yang kita kontrol sebagai output
    int out_pins[] = {
        p->bq_ce,
        p->pca_reset,
        p->ads131_sync_reset,
        p->ads1299_start,
        p->ads1299_reset,
        p->ads1299_pwdn,
        p->ads1299_cs,
        p->ads131_cs,
    };

    uint64_t out_mask = 0;
    for (size_t i = 0; i < sizeof(out_pins) / sizeof(out_pins[0]); i++) {
        if (out_pins[i] >= 0) out_mask |= (1ULL << (uint32_t)out_pins[i]);
    }

    if (out_mask != 0) {
        gpio_config_t io = {0};
        io.intr_type    = GPIO_INTR_DISABLE;
        io.mode         = GPIO_MODE_OUTPUT;
        io.pin_bit_mask = out_mask;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.pull_up_en   = GPIO_PULLUP_DISABLE;

        esp_err_t err = gpio_config(&io);
        if (err != ESP_OK) return err;
    }

    // slash: input pins (DRDY). Driver biopot/fnirs akan set interrupt edge sesuai kebutuhan.
    int in_pins[] = {
        p->ads1299_drdy,
        p->ads131_drdy,
    };

    uint64_t in_mask = 0;
    for (size_t i = 0; i < sizeof(in_pins) / sizeof(in_pins[0]); i++) {
        if (in_pins[i] >= 0) in_mask |= (1ULL << (uint32_t)in_pins[i]);
    }

    if (in_mask != 0) {
        gpio_config_t ii = {0};
        ii.intr_type    = GPIO_INTR_DISABLE;
        ii.mode         = GPIO_MODE_INPUT;
        ii.pin_bit_mask = in_mask;
        ii.pull_down_en = GPIO_PULLDOWN_DISABLE;
        ii.pull_up_en   = GPIO_PULLUP_DISABLE;

        esp_err_t err = gpio_config(&ii);
        if (err != ESP_OK) return err;
    }

    // Default output states
    // slash: guard pin supaya aman kalau suatu saat dibuat -1
    if (p->ads1299_cs >= 0) (void)gpio_set_level(p->ads1299_cs, 1); // slash: CS idle high
    if (p->ads131_cs  >= 0) (void)gpio_set_level(p->ads131_cs, 1);

    if (p->ads131_sync_reset >= 0) (void)gpio_set_level(p->ads131_sync_reset, 1); // slash: aktif low, jadi idle high

    if (p->ads1299_start >= 0) (void)gpio_set_level(p->ads1299_start, 0);
    if (p->ads1299_reset >= 0) (void)gpio_set_level(p->ads1299_reset, 1);
    if (p->ads1299_pwdn  >= 0) (void)gpio_set_level(p->ads1299_pwdn, 1);

    if (p->pca_reset >= 0) (void)gpio_set_level(p->pca_reset, 1); // slash: aktif low, idle high

    // slash: CE default kita set high, artinya "charger off" kalau CE active low
    // slash: nanti nocti_board_init_all akan panggil nocti_board_set_charger_enable(true)
    if (p->bq_ce >= 0) (void)gpio_set_level(p->bq_ce, 1);

    return ESP_OK;
}

// ============================================================
// HAL init
// ============================================================

static esp_err_t init_i2c(nocti_board_t *b)
{
    nocti_hal_i2c_cfg_t i2c_cfg = {
        .i2c_port               = b->cfg.i2c_port,
        .gpio_sda               = b->pins.i2c_sda,
        .gpio_scl               = b->pins.i2c_scl,
        .clk_speed_hz           = b->cfg.i2c_clk_hz,
        .trans_queue_depth       = b->cfg.i2c_trans_queue_depth,
        .enable_internal_pullups = b->cfg.i2c_enable_internal_pullups,
        .delete_driver_on_deinit = true, // slash: board owns driver lifetime
    };

    return nocti_hal_i2c_init(&b->i2c, &i2c_cfg);
}

static esp_err_t init_spi_ads1299(nocti_board_t *b, const nocti_board_spi_policy_t *p)
{
    // slash: fallback ini hanya agar init jalan, bisa diubah via reconfig saat idle
    const uint8_t  fallback_mode = 1;
    const uint32_t fallback_clk  = 2000000u;

    nocti_hal_spi_cfg_t c = {
        .host   = b->cfg.spi_host_ads1299,
        .gpio_mosi = b->pins.ads1299_mosi,
        .gpio_miso = b->pins.ads1299_miso,
        .gpio_sclk = b->pins.ads1299_sclk,
        .gpio_cs   = b->pins.ads1299_cs,

        .clock_hz  = choose_spi_clock(p->clock_hz, fallback_clk),
        .spi_mode  = choose_spi_mode(p->spi_mode, fallback_mode),
        .queue_size = choose_queue(p->queue_size, 1),

        .use_dma        = p->use_dma,
        .input_delay_ns = p->input_delay_ns,
        .free_bus_on_deinit = true,
    };

    return nocti_hal_spi_init(&b->spi_ads1299, &c);
}

static esp_err_t init_spi_ads131(nocti_board_t *b, const nocti_board_spi_policy_t *p)
{
    const uint8_t  fallback_mode = 1;
    const uint32_t fallback_clk  = 2000000u;

    nocti_hal_spi_cfg_t c = {
        .host   = b->cfg.spi_host_ads131,
        .gpio_mosi = b->pins.ads131_mosi,
        .gpio_miso = b->pins.ads131_miso,
        .gpio_sclk = b->pins.ads131_sclk,
        .gpio_cs   = b->pins.ads131_cs,

        .clock_hz  = choose_spi_clock(p->clock_hz, fallback_clk),
        .spi_mode  = choose_spi_mode(p->spi_mode, fallback_mode),
        .queue_size = choose_queue(p->queue_size, 1),

        .use_dma        = p->use_dma,
        .input_delay_ns = p->input_delay_ns,
        .free_bus_on_deinit = true,
    };

    return nocti_hal_spi_init(&b->spi_ads131, &c);
}

// ============================================================
// BQ24295 basic init (internal)
// ============================================================
//
// Target konfigurasi awal:
// - Matikan I2C watchdog supaya host tidak wajib kick tiap 40 detik
//   Data sheet: REG05[5:4] = 00 berarti watchdog disable.
//
// Slash: kita tidak utak atik limit arus, safety timer, SYS_MIN, dll di sini.
//        Fokus dulu ke hal yang bisa ganggu firmware (watchdog).
//

static esp_err_t init_bq24295_basic(nocti_board_t *b)
{
    if (!b || !b->inited) return ESP_ERR_INVALID_STATE;

    uint8_t addr = b->cfg.i2c_addrs.bq24295_7bit;
    if (addr == 0) {
        // slash: tidak ada alamat, anggap charger tidak dipakai
        NOCTI_LOGI(TAG, "bq24295: i2c addr 0, skip charger init");
        return ESP_OK;
    }

    uint8_t reg5 = 0;
    esp_err_t err = nocti_hal_i2c_read_u8(&b->i2c, addr, 0x05, &reg5, 50);
    if (err != ESP_OK) {
        NOCTI_LOGW(TAG, "bq24295: read REG05 failed: %s", esp_err_to_name(err));
        return err;
    }

    // slash: REG05[5:4] adalah WATCHDOG, 00 disable, 01 40 detik, 10 80, 11 160
    uint8_t new5 = reg5 & (uint8_t)~(uint8_t)(0x3u << 4); // clear bit 5 dan 4

    if (new5 != reg5) {
        err = nocti_hal_i2c_write_u8(&b->i2c, addr, 0x05, new5, 50);
        if (err != ESP_OK) {
            NOCTI_LOGW(TAG, "bq24295: write REG05 failed: %s", esp_err_to_name(err));
            return err;
        }
        NOCTI_LOGI(TAG, "bq24295: REG05 0x%02X -> 0x%02X (watchdog disabled)", reg5, new5);
    } else {
        NOCTI_LOGI(TAG, "bq24295: REG05 already 0x%02X (watchdog disabled)", reg5);
    }

    return ESP_OK;
}

// ============================================================
// Public helper
// ============================================================

void nocti_board_cfg_set_defaults(nocti_board_cfg_t *cfg)
{
    if (!cfg) return;

    memset(cfg, 0, sizeof(*cfg));

    // I2C
    cfg->i2c_port                   = I2C_NUM_0;
    cfg->i2c_clk_hz                  = 400000;
    cfg->i2c_trans_queue_depth        = 0;
    cfg->i2c_enable_internal_pullups  = false;

    // SPI host wajib diisi pemanggil (diset 0 untuk memaksa validasi di init)
    cfg->spi_host_ads1299 = (spi_host_device_t)0;
    cfg->spi_host_ads131  = (spi_host_device_t)0;

    // SPI policy default netral
    cfg->spi_ads1299 = spi_policy_default();
    cfg->spi_ads131  = spi_policy_default();

    // Power path
    cfg->charger_ce_active_high = false; // slash: default ikut datasheet BQ24295 (CE active low)

    // I2C address default
    cfg->i2c_addrs.pca9956b_7bit = 0x01u;
    cfg->i2c_addrs.max17048_7bit = 0x36u;
    cfg->i2c_addrs.bq24295_7bit  = 0x6Bu;
}

// ============================================================
// Public
// ============================================================

esp_err_t nocti_board_init(nocti_board_t *b, const nocti_board_cfg_t *cfg)
{
    if (!b || !cfg) return ESP_ERR_INVALID_ARG;

    // slash: host SPI wajib diset eksplisit
    if (!host_ok(cfg->spi_host_ads1299) || !host_ok(cfg->spi_host_ads131)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(b, 0, sizeof(*b));
    b->pins = default_pins_noctic_v1();
    b->cfg  = *cfg;

    // slash: kalau pemanggil lupa isi address, isi default aman
    if (b->cfg.i2c_addrs.pca9956b_7bit == 0) {
        b->cfg.i2c_addrs.pca9956b_7bit = 0x01u;
    }
    if (b->cfg.i2c_addrs.max17048_7bit == 0) {
        b->cfg.i2c_addrs.max17048_7bit = 0x36u;
    }
    if (b->cfg.i2c_addrs.bq24295_7bit == 0) {
        b->cfg.i2c_addrs.bq24295_7bit = 0x6Bu;
    }

    esp_err_t err = init_gpios(&b->pins);
    if (err != ESP_OK) return err;

    // slash: ISR service harus sekali saja, modul fnirs dan biopot akan add handler setelah ini
    err = gpio_install_isr_service(0);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK; // slash: service sudah terpasang, itu aman
    }
    if (err != ESP_OK) return err;

    err = init_i2c(b);
    if (err != ESP_OK) return err;

    // slash: SPI di init dengan policy awal, bisa diganti saat idle via API reconfig
    err = init_spi_ads1299(b, &b->cfg.spi_ads1299);
    if (err != ESP_OK) return err;

    err = init_spi_ads131(b, &b->cfg.spi_ads131);
    if (err != ESP_OK) return err;

    b->inited = true;

    NOCTI_LOGI(TAG, "board inited: %s", NOCTI_BOARD_NAME);
    return ESP_OK;
}

esp_err_t nocti_board_deinit(nocti_board_t *b)
{
    if (!b) return ESP_ERR_INVALID_ARG;
    if (!b->inited) return ESP_OK;

    (void)nocti_hal_spi_deinit(&b->spi_ads131);
    (void)nocti_hal_spi_deinit(&b->spi_ads1299);
    (void)nocti_hal_i2c_deinit(&b->i2c);

    // slash: gpio ISR service tidak dihapus di sini, biar tidak ganggu modul lain
    b->inited = false;
    return ESP_OK;
}

// ============================================================
// Init lengkap: board + charger
// ============================================================

esp_err_t nocti_board_init_all(nocti_board_t *b, nocti_board_cfg_t *cfg)
{
    if (!b || !cfg) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nocti_board_init(b, cfg);
    if (err != ESP_OK) return err;

    // slash: matikan watchdog BQ supaya firmware tidak wajib kick terus
    err = init_bq24295_basic(b);
    if (err != ESP_OK) {
        // slash: jangan bikin boot gagal hanya karena charger, cukup warning
        NOCTI_LOGW(TAG, "bq24295 basic init error: %s", esp_err_to_name(err));
    }

    // slash: setelah konfigurasi dasar, enable charger lewat CE
    (void)nocti_board_set_charger_enable(b, true);

    return ESP_OK;
}

// ============================================================
// Getters
// ============================================================

const nocti_board_pins_t *nocti_board_pins(const nocti_board_t *b)
{
    if (!b) return NULL;
    return &b->pins;
}

const nocti_board_i2c_addrs_t *nocti_board_i2c_addrs(const nocti_board_t *b)
{
    if (!b) return NULL;
    return &b->cfg.i2c_addrs;
}

const nocti_board_cfg_t *nocti_board_get_cfg(const nocti_board_t *b)
{
    if (!b) return NULL;
    return &b->cfg;
}

nocti_hal_i2c_t *nocti_board_i2c(nocti_board_t *b)
{
    if (!b) return NULL;
    return &b->i2c;
}

nocti_hal_spi_t *nocti_board_spi_ads1299(nocti_board_t *b)
{
    if (!b) return NULL;
    return &b->spi_ads1299;
}

nocti_hal_spi_t *nocti_board_spi_ads131(nocti_board_t *b)
{
    if (!b) return NULL;
    return &b->spi_ads131;
}

// ============================================================
// Board level controls
// ============================================================

esp_err_t nocti_board_set_charger_enable(nocti_board_t *b, bool enable)
{
    if (!b || !b->inited) return ESP_ERR_INVALID_STATE;
    if (b->pins.bq_ce < 0) return ESP_ERR_INVALID_STATE;

    // slash: interpretasi enable ke level CE tergantung polarity di cfg
    // slash: default BQ24295: CE active low, jadi enable berarti CE low
    bool active_high = b->cfg.charger_ce_active_high;

    int level;
    if (enable) {
        level = active_high ? 1 : 0;
    } else {
        level = active_high ? 0 : 1;
    }

    return gpio_set_level(b->pins.bq_ce, level);
}

// ============================================================
// SPI reconfig
// ============================================================
// slash: panggil hanya saat idle, tidak ada transaksi SPI yang berjalan
// slash: reconfig dengan cara deinit lalu init ulang device handle
// ============================================================

esp_err_t nocti_board_spi_reconfig_ads1299(nocti_board_t *b, const nocti_board_spi_policy_t *p)
{
    if (!b || !b->inited || !p) return ESP_ERR_INVALID_ARG;

    // slash: simpan policy baru untuk konsistensi debug
    b->cfg.spi_ads1299 = *p;

    (void)nocti_hal_spi_deinit(&b->spi_ads1299);
    return init_spi_ads1299(b, &b->cfg.spi_ads1299);
}

esp_err_t nocti_board_spi_reconfig_ads131(nocti_board_t *b, const nocti_board_spi_policy_t *p)
{
    if (!b || !b->inited || !p) return ESP_ERR_INVALID_ARG;

    b->cfg.spi_ads131 = *p;

    (void)nocti_hal_spi_deinit(&b->spi_ads131);
    return init_spi_ads131(b, &b->cfg.spi_ads131);
}

// ============================================================
// Optional debug
// ============================================================

esp_err_t nocti_board_i2c_probe_all(nocti_board_t *b, uint32_t timeout_ms)
{
    if (!b || !b->inited) return ESP_ERR_INVALID_STATE;

    for (uint8_t a = 1; a < 0x7F; a++) {
        esp_err_t err = nocti_hal_i2c_probe(&b->i2c, a, timeout_ms);
        if (err == ESP_OK) {
            NOCTI_LOGI(TAG, "i2c ack: 0x%02X", (unsigned)a);
        }
    }
    return ESP_OK;
}

esp_err_t nocti_board_bq24295_dump(nocti_board_t *b)
{
    if (!b || !b->inited) return ESP_ERR_INVALID_STATE;

    uint8_t addr = b->cfg.i2c_addrs.bq24295_7bit;
    if (addr == 0) {
        NOCTI_LOGW(TAG, "bq24295_dump: addr 0, skip");
        return ESP_OK;
    }

    for (uint8_t reg = 0; reg <= 0x09; reg++) {
        uint8_t val = 0;
        esp_err_t err = nocti_hal_i2c_read_u8(&b->i2c, addr, reg, &val, 50);
        if (err != ESP_OK) {
            NOCTI_LOGW(TAG, "bq24295_dump: read reg 0x%02X failed: %s", reg, esp_err_to_name(err));
            return err;
        }
        NOCTI_LOGI(TAG, "bq24295 REG0x%02X = 0x%02X", reg, val);
    }

    return ESP_OK;
}

// ============================================================
// Builder config
// ============================================================

esp_err_t nocti_board_build_fnirs_cfg(const nocti_board_t *b,
                                      nocti_fnirs_cfg_t *out_cfg,
                                      nocti_hal_spi_t *spi_ads131)
{
    if (!b || !b->inited) return ESP_ERR_INVALID_STATE;
    if (!out_cfg || !spi_ads131) return ESP_ERR_INVALID_ARG;

    memset(out_cfg, 0, sizeof(*out_cfg));

    // slash: SPI handle di inject, supaya builder tidak asumsi ownership
    out_cfg->spi = spi_ads131;

    // GPIO
    out_cfg->gpio_drdy = b->pins.ads131_drdy;

    // slash: ADS131M04 pakai 1 pin gabungan SYNC dan RESET (aktif low)
    // slash: jadi kedua field cfg menunjuk ke pin yang sama
    out_cfg->gpio_reset = b->pins.ads131_sync_reset;
    out_cfg->gpio_sync  = b->pins.ads131_sync_reset;

    // DRDY timeout:
    // Timeout yang terlalu kecil mudah bikin ESP_ERR_TIMEOUT walau ADC sebenarnya jalan,
    // karena task bisa miss edge DRDY (interrupt latency, scheduling).
    // Stand-alone bring-up menunggu DRDY sampai ratusan ms.
    // Untuk path library, set lebih longgar.
    out_cfg->drdy_timeout_us = 20000; // 20 ms
    out_cfg->spi_timeout_ms  = 20;

    // Queue internal default
    // slash: ini hanya buffer, bukan storage permanen
    out_cfg->queue_len = 64;

    // Register init:
    // Untuk menyamakan dengan stand-alone (yang sudah terbukti bisa baca frame tanpa
    // setting register), defaultnya kita OFF dulu. Kalau nanti perlu data-rate/gain,
    // baru diaktifkan.
    out_cfg->reg_mode  = 0;
    out_cfg->reg_clock = 0;
    out_cfg->reg_gain  = 0;

    out_cfg->do_adc_register_init = false;

    return ESP_OK;
}
