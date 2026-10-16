// ============================================================
// File: components/nocti_hal_spi/nocti_hal_spi.c
// ============================================================

#include "nocti_hal_spi.h"

#include <string.h>

#include "nocti_log.h"

static const char *TAG = "nocti_hal_spi";

static inline TickType_t ms_to_ticks(uint32_t ms)
{
    // /fix: kontrak API kita: timeout_ms = 0 artinya tunggu selamanya.
    // FreeRTOS: portMAX_DELAY dipakai untuk block tanpa batas.
    return (ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(ms);
}

// // batas aman transfer, harus selaras dengan buscfg.max_transfer_sz
#define NOCTI_SPI_MAX_TRANSFER_SZ   (256)

// // belajar: buffer kecil untuk clock-out nol saat read-only
#define NOCTI_SPI_ZEROS_CHUNK_SZ    (32)

// ============================================================
// Init SPI bus dan tambah satu device handle
// ============================================================

esp_err_t nocti_hal_spi_init(nocti_hal_spi_t *h, const nocti_hal_spi_cfg_t *cfg)
{
    if (!h || !cfg) return ESP_ERR_INVALID_ARG;

    // // validasi minimal pin bus
    if (cfg->gpio_mosi < 0 || cfg->gpio_miso < 0 || cfg->gpio_sclk < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(h, 0, sizeof(*h));
    h->host = cfg->host;
    h->gpio_cs = cfg->gpio_cs;
    h->use_dma = cfg->use_dma;
    h->free_bus_on_deinit = cfg->free_bus_on_deinit;
    h->bus_owned = false;

    h->mutex = xSemaphoreCreateMutex();
    if (!h->mutex) return ESP_ERR_NO_MEM;

    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg->gpio_mosi,
        .miso_io_num = cfg->gpio_miso,
        .sclk_io_num = cfg->gpio_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,

        // // belajar: max_transfer_sz membatasi ukuran transaksi, transfer besar harus di-chunk
        .max_transfer_sz = NOCTI_SPI_MAX_TRANSFER_SZ,
    };

    int dma_chan = cfg->use_dma ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED;

    esp_err_t err = spi_bus_initialize(cfg->host, &buscfg, dma_chan);

    if (err == ESP_OK) {
        h->bus_owned = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        // // bus sudah di-init pihak lain, kita tidak own
        // // belajar: pins dan max_transfer_sz harus sama dengan init pertama pada host itu
        h->bus_owned = false;
        NOCTI_LOGW(TAG,
                 "spi_bus_initialize: bus already initialized. pins and max_transfer_sz must match existing init");
    } else {
        NOCTI_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(h->mutex);
        memset(h, 0, sizeof(*h));
        return err;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)cfg->clock_hz,
        .mode = cfg->spi_mode,
        .spics_io_num = cfg->gpio_cs, // // -1 boleh, berarti CS manual di luar

        .queue_size = (cfg->queue_size == 0) ? 1 : cfg->queue_size,

        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,

        .duty_cycle_pos = 128,
        // Tambah guard kecil sebelum dan sesudah transaksi.
        // Ini meniru setting stand-alone ADS131 bring-up dan membantu device yang
        // butuh setup/hold di sekitar CS.
        .cs_ena_pretrans = 1,
        .cs_ena_posttrans = 1,

        .input_delay_ns = (cfg->input_delay_ns < 0) ? 0 : cfg->input_delay_ns,
    };

    err = spi_bus_add_device(cfg->host, &devcfg, &h->dev);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));

        if (h->bus_owned) {
            (void)spi_bus_free(cfg->host);
        }

        vSemaphoreDelete(h->mutex);
        memset(h, 0, sizeof(*h));
        return err;
    }

    return ESP_OK;
}

// ============================================================
// Deinit
// ============================================================

esp_err_t nocti_hal_spi_deinit(nocti_hal_spi_t *h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    if (!h->dev) return ESP_OK;

    esp_err_t err = spi_bus_remove_device(h->dev);
    if (err != ESP_OK) {
        NOCTI_LOGE(TAG, "spi_bus_remove_device failed: %s", esp_err_to_name(err));
        return err;
    }

    h->dev = NULL;

    if (h->free_bus_on_deinit && h->bus_owned) {
        // // belajar: spi_bus_free aman kalau host itu memang hanya dipakai instance ini
        err = spi_bus_free(h->host);
        if (err != ESP_OK) {
            NOCTI_LOGE(TAG, "spi_bus_free failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    if (h->mutex) {
        vSemaphoreDelete(h->mutex);
        h->mutex = NULL;
    }

    memset(h, 0, sizeof(*h));
    return ESP_OK;
}

// ============================================================
// Lock, unlock
// ============================================================

esp_err_t nocti_hal_spi_lock(nocti_hal_spi_t *h, uint32_t timeout_ms)
{
    if (!h || !h->mutex) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(h->mutex, ms_to_ticks(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void nocti_hal_spi_unlock(nocti_hal_spi_t *h)
{
    if (!h || !h->mutex) return;
    (void)xSemaphoreGive(h->mutex);
}

// ============================================================
// Transfer utama
// // belajar: selalu chunk supaya tidak nabrak max_transfer_sz dan lebih stabil untuk frame panjang
// ============================================================

static esp_err_t transfer_locked(nocti_hal_spi_t *h,
                                const void *tx, void *rx,
                                size_t len_bytes)
{
    if (!h || !h->dev) return ESP_ERR_INVALID_STATE;
    if (len_bytes == 0) return ESP_OK;

    const uint8_t *tx8 = (const uint8_t *)tx;
    uint8_t *rx8 = (uint8_t *)rx;

    // /note: buffer nol kecil untuk kasus read-only (tx null).
    // Untuk transfer kontigu besar, minta caller kirim tx berisi nol sendiri.
    uint8_t zeros[NOCTI_SPI_ZEROS_CHUNK_SZ];
    memset(zeros, 0, sizeof(zeros));

    size_t done = 0;
    esp_err_t err = ESP_OK;

    while (done < len_bytes && err == ESP_OK) {
        size_t chunk = len_bytes - done;
        if (chunk > NOCTI_SPI_MAX_TRANSFER_SZ) chunk = NOCTI_SPI_MAX_TRANSFER_SZ;

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = chunk * 8;

        if (tx8) {
            t.tx_buffer = tx8 + done;
        } else {
            // /note: kalau tx null, clock out nol.
            // zeros cuma 32 byte, jadi read-only akan ter-chunk.
            if (chunk > sizeof(zeros)) chunk = sizeof(zeros);
            t.length = chunk * 8;
            t.tx_buffer = zeros;
        }

        t.rx_buffer = rx8 ? (rx8 + done) : NULL;

        err = spi_device_transmit(h->dev, &t);
        done += chunk;
    }

    return err;
}

// ============================================================
// Transfer kontigu (paksa 1 transaksi SPI saja)
// /fix: versi ini tidak mengubah chunk untuk tx NULL.
// /note: untuk read-only kontigu, caller wajib menyiapkan tx buffer berisi nol.
// ============================================================

static esp_err_t transfer_contig_locked(nocti_hal_spi_t *h,
                                       const void *tx, void *rx,
                                       size_t len_bytes)
{
    if (!h || !h->dev) return ESP_ERR_INVALID_STATE;
    if (len_bytes == 0) return ESP_OK;
    if (len_bytes > NOCTI_SPI_MAX_TRANSFER_SZ) return ESP_ERR_INVALID_SIZE;

    // /safe: agar benar-benar kontigu, tx wajib ada.
    // Kalau butuh read-only, kirim tx buffer nol dari caller.
    if (!tx) return ESP_ERR_INVALID_ARG;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len_bytes * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    // /jitter: polling_transmit cenderung lebih deterministik untuk frame kecil.
    return spi_device_polling_transmit(h->dev, &t);
}

static esp_err_t read_locked(nocti_hal_spi_t *h, void *rx, size_t len_bytes)
{
    if (!h || !h->dev || !rx) return ESP_ERR_INVALID_ARG;
    if (len_bytes == 0) return ESP_OK;

    // // reuse transfer_locked dengan tx null
    return transfer_locked(h, NULL, rx, len_bytes);
}

esp_err_t nocti_hal_spi_transfer(nocti_hal_spi_t *h,
                                const void *tx, void *rx,
                                size_t len_bytes,
                                uint32_t timeout_ms)
{
    if (!h) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nocti_hal_spi_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    err = transfer_locked(h, tx, rx, len_bytes);

    nocti_hal_spi_unlock(h);
    return err;
}

// ============================================================
// Transfer kontigu (paksa 1 transaksi SPI)
// /note: timeout_ms hanya dipakai untuk mutex lock.
// ============================================================

esp_err_t nocti_hal_spi_transfer_contig(nocti_hal_spi_t *h,
                                       const void *tx, void *rx,
                                       size_t len_bytes,
                                       uint32_t timeout_ms)
{
    if (!h) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nocti_hal_spi_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    err = transfer_contig_locked(h, tx, rx, len_bytes);

    nocti_hal_spi_unlock(h);
    return err;
}

// ============================================================
// Helper write
// ============================================================

esp_err_t nocti_hal_spi_write(nocti_hal_spi_t *h,
                             const void *tx, size_t len_bytes,
                             uint32_t timeout_ms)
{
    return nocti_hal_spi_transfer(h, tx, NULL, len_bytes, timeout_ms);
}

// ============================================================
// Helper read
// ============================================================

esp_err_t nocti_hal_spi_read(nocti_hal_spi_t *h,
                            void *rx, size_t len_bytes,
                            uint32_t timeout_ms)
{
    if (!h || !rx) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nocti_hal_spi_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    err = read_locked(h, rx, len_bytes);

    nocti_hal_spi_unlock(h);
    return err;
}

// ============================================================
// Write then read (dua transaksi)
// ============================================================

esp_err_t nocti_hal_spi_write_then_read(nocti_hal_spi_t *h,
                                       const void *tx, size_t tx_len,
                                       void *rx, size_t rx_len,
                                       uint32_t timeout_ms)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    if (tx_len == 0 && rx_len == 0) return ESP_OK;

    esp_err_t err = nocti_hal_spi_lock(h, timeout_ms);
    if (err != ESP_OK) return err;

    if (tx_len) {
        err = transfer_locked(h, tx, NULL, tx_len);
    }

    if (err == ESP_OK && rx_len) {
        err = read_locked(h, rx, rx_len);
    }

    nocti_hal_spi_unlock(h);
    return err;
}

