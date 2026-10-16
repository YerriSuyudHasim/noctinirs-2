#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "nocti_v3_ads131m04.h"

#include <string.h>

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "nocti_v3_ads131";

#define ADS_REG_ID       0x00u
#define ADS_REG_STATUS   0x01u
#define ADS_REG_MODE     0x02u
#define ADS_REG_CLOCK    0x03u
#define ADS_REG_GAIN     0x04u
#define ADS_REG_CFG      0x06u

#define ADS_CMD_NULL     0x0000u
#define ADS_CMD_UNLOCK   0x0655u

#define ADS_MODE_WLENGTH_MASK  0x0300u
#define ADS_MODE_WLENGTH_24    0x0100u
#define ADS_MODE_RX_CRC_MASK   0x1000u
#define ADS_MODE_RESET_MASK    0x0400u
#define ADS_MODE_CRC_TYPE_MASK 0x0800u
#define ADS_MODE_DRDY_HIZ_MASK 0x0002u
#define ADS_MODE_DRDY_FMT_MASK 0x0001u
#define ADS_MODE_DRDY_PULSE    0x0001u

#define ADS_ID_CHANCNT_MASK    0x0F00u
#define ADS_ID_CHANCNT_4       0x0400u
#define ADS_RESET_RESPONSE     0xFF24u
#define ADS_STATUS_LOCK_MASK      0x8000u
#define ADS_STATUS_F_RESYNC_MASK  0x4000u
#define ADS_STATUS_REG_MAP_MASK   0x2000u
#define ADS_STATUS_CRC_ERR_MASK   0x1000u
#define ADS_STATUS_CRC_TYPE_MASK  0x0800u
#define ADS_STATUS_RESET_MASK     0x0400u
#define ADS_STATUS_HARD_FAULT_MASK \
    (ADS_STATUS_LOCK_MASK | ADS_STATUS_REG_MAP_MASK | \
     ADS_STATUS_CRC_ERR_MASK | ADS_STATUS_RESET_MASK)

#define ADS_CMD_RREG(addr, n_minus1) \
    ((uint16_t)((0x5u << 13) | (((uint16_t)(addr) & 0x3Fu) << 7) | \
                ((uint16_t)(n_minus1) & 0x7Fu)))
#define ADS_CMD_WREG(addr, n_minus1) \
    ((uint16_t)((0x3u << 13) | (((uint16_t)(addr) & 0x3Fu) << 7) | \
                ((uint16_t)(n_minus1) & 0x7Fu)))

_Static_assert(ADS_CMD_RREG(ADS_REG_MODE, 0u) == 0xA100u,
               "ADS131 RREG address field must be shifted by seven");
_Static_assert(ADS_CMD_RREG(ADS_REG_CLOCK, 0u) == 0xA180u,
               "ADS131 RREG CLOCK must be 0xA180");
_Static_assert(ADS_CMD_WREG(ADS_REG_MODE, 0u) == 0x6100u,
               "ADS131 WREG MODE must be 0x6100");
_Static_assert(ADS_CMD_WREG(ADS_REG_CLOCK, 0u) == 0x6180u,
               "ADS131 WREG CLOCK must be 0x6180");

static void pack_command(uint16_t command, uint8_t *out_word)
{
    out_word[0] = (uint8_t)(command >> 8);
    out_word[1] = (uint8_t)(command & 0xFFu);
    out_word[2] = 0u;
}

static uint32_t unpack_u24(const uint8_t *word)
{
    return ((uint32_t)word[0] << 16) |
           ((uint32_t)word[1] << 8) |
           (uint32_t)word[2];
}

static int32_t sign_extend_24(uint32_t value)
{
    return (value & 0x800000u) != 0u ?
        (int32_t)(value | 0xFF000000u) : (int32_t)value;
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t transfer_frame(nocti_v3_ads131m04_t *ads)
{
    memset(ads->rx, 0, sizeof(ads->rx));
    ads->transaction.length = NOCTI_V3_ADS_FRAME_BYTES * 8u;
    ads->transaction.rxlength = NOCTI_V3_ADS_FRAME_BYTES * 8u;
    ads->transaction.tx_buffer = ads->tx;
    ads->transaction.rx_buffer = ads->rx;
    return spi_device_polling_transmit(ads->device, &ads->transaction);
}

static esp_err_t simple_command(nocti_v3_ads131m04_t *ads, uint16_t command)
{
    memset(ads->tx, 0, sizeof(ads->tx));
    pack_command(command, &ads->tx[0]);
    return transfer_frame(ads);
}

static esp_err_t read_register(nocti_v3_ads131m04_t *ads,
                               uint8_t address,
                               uint16_t *out_value)
{
    if (out_value == NULL) return ESP_ERR_INVALID_ARG;
    memset(ads->tx, 0, sizeof(ads->tx));
    pack_command(ADS_CMD_RREG(address, 0u), &ads->tx[0]);
    esp_err_t err = transfer_frame(ads);
    if (err != ESP_OK) return err;

    memset(ads->tx, 0, sizeof(ads->tx));
    pack_command(ADS_CMD_NULL, &ads->tx[0]);
    err = transfer_frame(ads);
    if (err != ESP_OK) return err;
    *out_value = (uint16_t)(unpack_u24(&ads->rx[0]) >> 8);
    return ESP_OK;
}

static esp_err_t write_register(nocti_v3_ads131m04_t *ads,
                                uint8_t address,
                                uint16_t value)
{
    memset(ads->tx, 0, sizeof(ads->tx));
    pack_command(ADS_CMD_WREG(address, 0u), &ads->tx[0]);
    ads->tx[3] = (uint8_t)(value >> 8);
    ads->tx[4] = (uint8_t)(value & 0xFFu);
    ads->tx[5] = 0u;
    esp_err_t err = transfer_frame(ads);
    if (err != ESP_OK) return err;

    uint16_t readback = 0u;
    err = read_register(ads, address, &readback);
    if (err != ESP_OK) return err;
    if (readback != value) {
        ESP_LOGE(TAG,
                 "register verify failed addr=0x%02X wrote=0x%04X read=0x%04X",
                 (unsigned)address,
                 (unsigned)value,
                 (unsigned)readback);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t nocti_v3_ads131m04_init(nocti_v3_ads131m04_t *ads,
                                  nocti_v3_board_t *board,
                                  nocti_v3_timebase_t *timebase)
{
    if (ads == NULL || board == NULL || timebase == NULL) return ESP_ERR_INVALID_ARG;
    if (board->config.ads131_spi_host != SPI3_HOST ||
        board->config.ads131_spi_mode != 1u ||
        board->config.ads131_spi_clock_hz != NOCTI_V3_ADS_SPI_CLOCK_HZ) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ads, 0, sizeof(*ads));
    ads->board = board;
    ads->timebase = timebase;

    const spi_bus_config_t bus = {
        .mosi_io_num = board->pins.ads131_mosi,
        .miso_io_num = board->pins.ads131_miso,
        .sclk_io_num = board->pins.ads131_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 256,
        /* Match the hardware-validated V2 HAL bus configuration. */
    };
    esp_err_t err = spi_bus_initialize(board->config.ads131_spi_host,
                                       &bus,
                                       SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;
    ads->bus_initialized = true;

    const spi_device_interface_config_t device = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = board->config.ads131_spi_mode,
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 1,
        .cs_ena_posttrans = 1,
        .clock_speed_hz = (int)board->config.ads131_spi_clock_hz,
        .input_delay_ns = 0,
        .spics_io_num = board->pins.ads131_cs,
        .flags = 0,
        .queue_size = 1,
        .pre_cb = NULL,
        .post_cb = NULL,
    };
    err = spi_bus_add_device(board->config.ads131_spi_host, &device, &ads->device);
    if (err != ESP_OK) {
        (void)spi_bus_free(board->config.ads131_spi_host);
        ads->bus_initialized = false;
        return err;
    }
    ads->device_added = true;
    memset(&ads->transaction, 0, sizeof(ads->transaction));

    ESP_LOGI(TAG,
             "SPI ready host=SPI3 mode=1 clock=%lu Hz frame=%u bytes DMA=off cs_guard=1/1 V2_port=1",
             (unsigned long)board->config.ads131_spi_clock_hz,
             (unsigned)NOCTI_V3_ADS_FRAME_BYTES);
    return ESP_OK;
}

esp_err_t nocti_v3_ads131m04_acquire_bus(nocti_v3_ads131m04_t *ads)
{
    if (ads == NULL || !ads->device_added || ads->device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ads->bus_acquired) return ESP_OK;

    /* ESP-IDF 5.5.1 only accepts portMAX_DELAY for
     * spi_device_acquire_bus(). SPI3_HOST is dedicated to ADS131M04 and the
     * ADC task is its sole owner, so holding the bus for startup validation
     * or for one active acquisition session is deterministic. Session STOP
     * releases it through nocti_v3_ads131m04_release_bus().
     */
    const esp_err_t err = spi_device_acquire_bus(ads->device, portMAX_DELAY);
    if (err == ESP_OK) ads->bus_acquired = true;
    return err;
}

void nocti_v3_ads131m04_release_bus(nocti_v3_ads131m04_t *ads)
{
    if (ads != NULL && ads->bus_acquired && ads->device != NULL) {
        spi_device_release_bus(ads->device);
        ads->bus_acquired = false;
    }
}

esp_err_t nocti_v3_ads131m04_hardware_reset(nocti_v3_ads131m04_t *ads)
{
    if (ads == NULL || ads->board == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nocti_v3_board_assert_ads131_reset(ads->board);
    if (err != ESP_OK) return err;
    /* 300 us exceeds 2048 CLKIN periods at the validated 8.192 MHz clock. */
    esp_rom_delay_us(300u);
    err = nocti_v3_board_release_ads131_reset(ads->board);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(20u);

    memset(ads->tx, 0, sizeof(ads->tx));
    pack_command(ADS_CMD_NULL, &ads->tx[0]);
    err = transfer_frame(ads);
    if (err != ESP_OK) return err;
    const uint16_t response = (uint16_t)(unpack_u24(&ads->rx[0]) >> 8);
    if (response != ADS_RESET_RESPONSE) {
        ESP_LOGE(TAG,
                 "unexpected first response after reset expected=0x%04X got=0x%04X",
                 (unsigned)ADS_RESET_RESPONSE,
                 (unsigned)response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t nocti_v3_ads131m04_sync(nocti_v3_ads131m04_t *ads)
{
    if (ads == NULL || ads->board == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nocti_v3_board_set_ads131_sync_reset(ads->board, false);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(5u);
    return nocti_v3_board_set_ads131_sync_reset(ads->board, true);
}

esp_err_t nocti_v3_ads131m04_read_registers(nocti_v3_ads131m04_t *ads,
                                            nocti_v3_ads_registers_t *out_regs)
{
    if (ads == NULL || out_regs == NULL) return ESP_ERR_INVALID_ARG;
    nocti_v3_ads_registers_t regs = {0};
    esp_err_t err = read_register(ads, ADS_REG_ID, &regs.id);
    if (err == ESP_OK) err = read_register(ads, ADS_REG_STATUS, &regs.status);
    if (err == ESP_OK) err = read_register(ads, ADS_REG_MODE, &regs.mode);
    if (err == ESP_OK) err = read_register(ads, ADS_REG_CLOCK, &regs.clock);
    if (err == ESP_OK) err = read_register(ads, ADS_REG_GAIN, &regs.gain);
    if (err == ESP_OK) err = read_register(ads, ADS_REG_CFG, &regs.cfg);
    if (err != ESP_OK) return err;
    ads->registers = regs;
    *out_regs = regs;
    return ESP_OK;
}

esp_err_t nocti_v3_ads131m04_configure_8ksps(nocti_v3_ads131m04_t *ads)
{
    if (ads == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = simple_command(ads, ADS_CMD_UNLOCK);
    if (err != ESP_OK) return err;

    uint16_t mode = 0u;
    err = read_register(ads, ADS_REG_MODE, &mode);
    if (err != ESP_OK) return err;
    mode &= (uint16_t)~ADS_MODE_WLENGTH_MASK;
    mode |= ADS_MODE_WLENGTH_24;
    mode &= (uint16_t)~ADS_MODE_RX_CRC_MASK;
    mode &= (uint16_t)~ADS_MODE_RESET_MASK; /* Write zero to acknowledge reset. */
    mode &= (uint16_t)~ADS_MODE_CRC_TYPE_MASK;
    mode &= (uint16_t)~ADS_MODE_DRDY_HIZ_MASK;
    mode |= ADS_MODE_DRDY_PULSE;

    err = write_register(ads, ADS_REG_MODE, mode);
    if (err == ESP_OK) {
        err = write_register(ads,
                             ADS_REG_CLOCK,
                             NOCTI_V3_ADS_CLOCK_REGISTER_8KSPS);
    }
    if (err == ESP_OK) {
        err = write_register(ads,
                             ADS_REG_GAIN,
                             NOCTI_V3_ADS_GAIN_REGISTER_X1);
    }
    if (err == ESP_OK) {
        err = write_register(ads,
                             ADS_REG_CFG,
                             NOCTI_V3_ADS_CFG_CONTINUOUS);
    }
    if (err != ESP_OK) return err;

    nocti_v3_ads_registers_t regs;
    err = nocti_v3_ads131m04_read_registers(ads, &regs);
    if (err != ESP_OK) return err;
    if ((regs.id & ADS_ID_CHANCNT_MASK) != ADS_ID_CHANCNT_4 ||
        (regs.status & ADS_MODE_WLENGTH_MASK) != ADS_MODE_WLENGTH_24 ||
        (regs.mode & ADS_MODE_WLENGTH_MASK) != ADS_MODE_WLENGTH_24 ||
        (regs.mode & ADS_MODE_RX_CRC_MASK) != 0u ||
        (regs.mode & ADS_MODE_CRC_TYPE_MASK) != 0u ||
        (regs.mode & ADS_MODE_DRDY_HIZ_MASK) != 0u ||
        (regs.mode & ADS_MODE_DRDY_FMT_MASK) != ADS_MODE_DRDY_PULSE ||
        regs.clock != NOCTI_V3_ADS_CLOCK_REGISTER_8KSPS ||
        regs.gain != NOCTI_V3_ADS_GAIN_REGISTER_X1 ||
        regs.cfg != NOCTI_V3_ADS_CFG_CONTINUOUS) {
        ESP_LOGE(TAG,
                 "register contract failed ID=%04X STATUS=%04X MODE=%04X CLOCK=%04X GAIN=%04X CFG=%04X",
                 regs.id, regs.status, regs.mode, regs.clock, regs.gain, regs.cfg);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "registers verified ID=%04X STATUS=%04X MODE=%04X CLOCK=%04X GAIN=%04X CFG=%04X",
             regs.id, regs.status, regs.mode, regs.clock, regs.gain, regs.cfg);
    return ESP_OK;
}

esp_err_t nocti_v3_ads131m04_read_frame(nocti_v3_ads131m04_t *ads,
                                        nocti_v3_ads_frame_t *out_frame,
                                        uint32_t *out_spi_duration_us,
                                        uint64_t *out_spi_complete_timestamp_us)
{
    if (ads == NULL || out_frame == NULL) return ESP_ERR_INVALID_ARG;
    memset(ads->tx, 0, sizeof(ads->tx));

    uint64_t start_us = 0u;
    uint64_t end_us = 0u;
    (void)nocti_v3_timebase_now_us(ads->timebase, &start_us);
    const esp_err_t err = transfer_frame(ads);
    (void)nocti_v3_timebase_now_us(ads->timebase, &end_us);
    if (out_spi_duration_us != NULL) {
        *out_spi_duration_us = end_us >= start_us ?
            (uint32_t)(end_us - start_us) : 0u;
    }
    if (out_spi_complete_timestamp_us != NULL) {
        *out_spi_complete_timestamp_us = end_us;
    }
    if (err != ESP_OK) return err;

    nocti_v3_ads_frame_t frame = {0};
    const uint32_t status24 = unpack_u24(&ads->rx[0]);
    frame.response = (uint16_t)(status24 >> 8);
    frame.status = frame.response;
    for (unsigned channel = 0; channel < 4u; ++channel) {
        frame.channel[channel] = sign_extend_24(
            unpack_u24(&ads->rx[(channel + 1u) * NOCTI_V3_ADS_WORD_BYTES]));
    }
    frame.crc_received = (uint16_t)(unpack_u24(&ads->rx[15]) >> 8);
    frame.crc_calculated = crc16_ccitt(ads->rx, 15u);
    frame.crc_valid = frame.crc_received == frame.crc_calculated;
    frame.resync_occurred =
        (frame.status & ADS_STATUS_F_RESYNC_MASK) != 0u;
    /* F_RESYNC is intentionally classified separately. A host-issued SYNC
     * pulse is asynchronous to CLKIN and can legitimately set this one-shot
     * indication on the first response. The ADC task grants a bounded
     * allowance only during the post-SYNC settling window. LOCK, REG_MAP,
     * CRC_ERR, RESET, CRC type, and word-length mismatches remain hard faults.
     */
    frame.status_valid =
        (frame.status & ADS_MODE_WLENGTH_MASK) == ADS_MODE_WLENGTH_24 &&
        (frame.status & ADS_STATUS_CRC_TYPE_MASK) == 0u &&
        (frame.status & ADS_STATUS_HARD_FAULT_MASK) == 0u;
    *out_frame = frame;
    return ESP_OK;
}

esp_err_t nocti_v3_ads131m04_deinit(nocti_v3_ads131m04_t *ads)
{
    if (ads == NULL) return ESP_ERR_INVALID_ARG;
    nocti_v3_ads131m04_release_bus(ads);
    (void)nocti_v3_board_assert_ads131_reset(ads->board);

    esp_err_t first = ESP_OK;
    if (ads->device_added && ads->device != NULL) {
        const esp_err_t err = spi_bus_remove_device(ads->device);
        if (err != ESP_OK) first = err;
        ads->device = NULL;
        ads->device_added = false;
    }
    if (ads->bus_initialized && ads->board != NULL) {
        const esp_err_t err = spi_bus_free(ads->board->config.ads131_spi_host);
        if (err != ESP_OK && first == ESP_OK) first = err;
        ads->bus_initialized = false;
    }
    return first;
}
