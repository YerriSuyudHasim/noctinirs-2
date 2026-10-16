// ============================================================
// File: components/nocti_biopot/nocti_biopot.c
// Revisi fokus:
// 1) DRDY wait_task aman terhadap race ISR (volatile + critical section)
// 2) impedance_check: mode DC tidak menjalankan Goertzel (hindari hasil palsu)
// 3) kecilkan kemungkinan notify nyasar, dan reset notify state dengan benar
//
// slash:
// /belajar: ADS1299 SPI mode CPOL=0 CPHA=1, DRDY active low
// /belajar: CONFIG3 reserved bits 6:5 harus 1 1 (datasheet ADS1299) :contentReference[oaicite:0]{index=0}
// ============================================================

#include "nocti_biopot.h"

#include <string.h>
#include <math.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"

#include "nocti_log.h"
#include "nocti_timebase.h"

static const char *TAG = "nocti_biopot";
static portMUX_TYPE s_wait_mux = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
// ADS1299 command opcodes
// ============================================================
#define ADS1299_CMD_WAKEUP   0x02
#define ADS1299_CMD_STANDBY  0x04
#define ADS1299_CMD_RESET    0x06
#define ADS1299_CMD_START    0x08
#define ADS1299_CMD_STOP     0x0A
#define ADS1299_CMD_RDATAC   0x10
#define ADS1299_CMD_SDATAC   0x11
#define ADS1299_CMD_RDATA    0x12

#define ADS1299_CMD_RREG(a)  (uint8_t)(0x20u | ((a) & 0x1Fu))
#define ADS1299_CMD_WREG(a)  (uint8_t)(0x40u | ((a) & 0x1Fu))

// ============================================================
// ADS1299 register map (subset)
// ============================================================
#define ADS1299_REG_ID           0x00
#define ADS1299_REG_CONFIG1      0x01
#define ADS1299_REG_CONFIG2      0x02
#define ADS1299_REG_CONFIG3      0x03
#define ADS1299_REG_LOFF         0x04
#define ADS1299_REG_CH1SET       0x05
#define ADS1299_REG_CH2SET       0x06
#define ADS1299_REG_CH3SET       0x07
#define ADS1299_REG_CH4SET       0x08
#define ADS1299_REG_CH5SET       0x09
#define ADS1299_REG_CH6SET       0x0A
#define ADS1299_REG_CH7SET       0x0B
#define ADS1299_REG_CH8SET       0x0C
#define ADS1299_REG_BIAS_SENSP   0x0D
#define ADS1299_REG_BIAS_SENSN   0x0E
#define ADS1299_REG_LOFF_SENSP   0x0F
#define ADS1299_REG_LOFF_SENSN   0x10
#define ADS1299_REG_LOFF_STATP   0x12
#define ADS1299_REG_LOFF_STATN   0x13
#define ADS1299_REG_MISC1        0x15
#define ADS1299_REG_CONFIG4      0x17

// ============================================================
// Internal context
// ============================================================

typedef struct {
    nocti_biopot_cfg_t cfg;
    bool inited;
    bool streaming;

    // slash: BIAS sense override
    // /belajar: kalau enable true, driver akan pakai mask ini apa adanya
    bool bias_override;
    uint8_t bias_sensp_override;
    uint8_t bias_sensn_override;

    // DRDY sync
    // /belajar: handle task yang sedang menunggu DRDY, diakses via critical section
    volatile TaskHandle_t wait_task;

    // Optional queue
    QueueHandle_t q;

    // Scratch buffers
    uint8_t tx[2 + 32];
    uint8_t rx[2 + 32];

    uint8_t frame_tx[NOCTI_BIOPOT_FRAME_BYTES];
    uint8_t frame_rx[NOCTI_BIOPOT_FRAME_BYTES];
} biopot_ctx_t;

static biopot_ctx_t s;

// slash: forward declaration
// /belajar: guard function untuk memastikan perubahan register hanya saat idle
static esp_err_t ensure_idle(void);

// ============================================================
// Small helpers
// ============================================================

static inline uint32_t u24_be(const uint8_t *p3)
{
    return ((uint32_t)p3[0] << 16) | ((uint32_t)p3[1] << 8) | (uint32_t)p3[2];
}

static inline int32_t s24_to_s32(uint32_t v24)
{
    if (v24 & 0x800000u) {
        return (int32_t)(v24 | 0xFF000000u);
    }
    return (int32_t)v24;
}

static inline uint8_t gain_to_code(nocti_biopot_gain_t g)
{
    switch (g) {
        case NOCTI_BIOPOT_GAIN_1:  return 0;
        case NOCTI_BIOPOT_GAIN_2:  return 1;
        case NOCTI_BIOPOT_GAIN_4:  return 2;
        case NOCTI_BIOPOT_GAIN_6:  return 3;
        case NOCTI_BIOPOT_GAIN_8:  return 4;
        case NOCTI_BIOPOT_GAIN_12: return 5;
        case NOCTI_BIOPOT_GAIN_24: return 6;
        default: return 0;
    }
}

static inline float gain_to_float(nocti_biopot_gain_t g)
{
    return (float)g;
}

// ============================================================
// SPI primitives
// ============================================================

static esp_err_t spi_xfer(const void *tx, void *rx, size_t n)
{
    if (n <= 32) {
        // slash: pakai polling untuk frame kecil agar deterministik
        return nocti_hal_spi_transfer_contig(s.cfg.spi, tx, rx, n, s.cfg.spi_timeout_ms);
    }
    return nocti_hal_spi_transfer(s.cfg.spi, tx, rx, n, s.cfg.spi_timeout_ms);
}

static esp_err_t ads1299_send_cmd(uint8_t cmd)
{
    uint8_t txb = cmd;
    uint8_t rxb = 0;
    return spi_xfer(&txb, &rxb, 1);
}

static esp_err_t ads1299_rreg(uint8_t addr, uint8_t *out, uint8_t n)
{
    if (!out || n == 0) return ESP_ERR_INVALID_ARG;
    if (n > 32) return ESP_ERR_INVALID_ARG;

    s.tx[0] = ADS1299_CMD_RREG(addr);
    s.tx[1] = (uint8_t)(n - 1u);
    memset(&s.tx[2], 0, n);

    esp_err_t err = spi_xfer(s.tx, s.rx, (size_t)(2u + n));
    if (err != ESP_OK) return err;

    memcpy(out, &s.rx[2], n);
    return ESP_OK;
}

static esp_err_t ads1299_wreg(uint8_t addr, const uint8_t *in, uint8_t n)
{
    if (!in || n == 0) return ESP_ERR_INVALID_ARG;
    if (n > 32) return ESP_ERR_INVALID_ARG;

    s.tx[0] = ADS1299_CMD_WREG(addr);
    s.tx[1] = (uint8_t)(n - 1u);
    memcpy(&s.tx[2], in, n);

    return spi_xfer(s.tx, s.rx, (size_t)(2u + n));
}

static esp_err_t reg_read_u8(uint8_t addr, uint8_t *v)
{
    return ads1299_rreg(addr, v, 1);
}

static esp_err_t reg_write_u8(uint8_t addr, uint8_t v)
{
    return ads1299_wreg(addr, &v, 1);
}

// ============================================================
// DRDY ISR
// ============================================================

static void IRAM_ATTR drdy_isr(void *arg)
{
    (void)arg;

    BaseType_t hp = pdFALSE;

    taskENTER_CRITICAL_ISR(&s_wait_mux);
    TaskHandle_t t = (TaskHandle_t)s.wait_task;
    taskEXIT_CRITICAL_ISR(&s_wait_mux);

    if (t) {
        vTaskNotifyGiveFromISR(t, &hp);
    }
    if (hp) {
        portYIELD_FROM_ISR();
    }
}

static inline void wait_task_set(TaskHandle_t t)
{
    taskENTER_CRITICAL(&s_wait_mux);
    s.wait_task = t;
    taskEXIT_CRITICAL(&s_wait_mux);
}

static inline void wait_task_clear(void)
{
    taskENTER_CRITICAL(&s_wait_mux);
    s.wait_task = NULL;
    taskEXIT_CRITICAL(&s_wait_mux);
}

static esp_err_t wait_drdy_low(uint32_t timeout_us)
{
    // slash: fast path
    if (gpio_get_level(s.cfg.gpio_drdy) == 0) return ESP_OK;

    // slash: pastikan tidak ada task lain yang sedang menunggu
    taskENTER_CRITICAL(&s_wait_mux);
    bool busy = (s.wait_task != NULL);
    taskEXIT_CRITICAL(&s_wait_mux);
    if (busy) return ESP_ERR_INVALID_STATE;

    TaskHandle_t me = xTaskGetCurrentTaskHandle();

    // slash: drain notify supaya tidak kebawa event lama
    // /belajar: pdTRUE = clear on exit, tapi kita tetap drain di awal untuk safety
    (void)ulTaskNotifyTake(pdTRUE, 0);

    wait_task_set(me);

    // slash: re-check setelah set handle, cegah race
    if (gpio_get_level(s.cfg.gpio_drdy) == 0) {
        wait_task_clear();
        return ESP_OK;
    }

    TickType_t ticks = portMAX_DELAY;
    if (timeout_us > 0) {
        uint32_t ms = (timeout_us + 999u) / 1000u;
        ticks = pdMS_TO_TICKS(ms);
        if (ticks == 0) ticks = 1;
    }

    uint32_t got = ulTaskNotifyTake(pdTRUE, ticks);
    wait_task_clear();

    if (got == 0) return ESP_ERR_TIMEOUT;
    if (gpio_get_level(s.cfg.gpio_drdy) != 0) return ESP_ERR_TIMEOUT;

    return ESP_OK;
}

// ============================================================
// Channel register build
// ============================================================

static uint8_t build_chset(const nocti_biopot_ch_cfg_t *c)
{
    uint8_t v = 0;

    if (!c->enable) v |= 0x80;
    v |= (uint8_t)(gain_to_code(c->gain) << 4);
    if (c->srb2) v |= 0x08;
    v |= (uint8_t)(c->mux & 0x07u);

    return v;
}

static uint8_t ch_reg_addr(uint8_t ch1_to_8)
{
    return (uint8_t)(ADS1299_REG_CH1SET + (ch1_to_8 - 1u));
}

static void build_bias_sense(uint8_t *out_sensp, uint8_t *out_sensn)
{
    // slash: override menang
    if (s.bias_override) {
        *out_sensp = s.bias_sensp_override;
        *out_sensn = s.bias_sensn_override;
        return;
    }

    uint8_t p = 0;
    uint8_t n = 0;

    for (uint8_t i = 0; i < NOCTI_BIOPOT_NUM_CH; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        if (s.cfg.ch[i].enable) {
            p |= bit;
            if (!s.cfg.ch[i].srb2) n |= bit;
        }
    }

    *out_sensp = p;
    *out_sensn = n;
}

// ============================================================
// slash: config defaults
// ============================================================

void nocti_biopot_cfg_set_defaults(nocti_biopot_cfg_t *cfg)
{
    if (!cfg) return;

    memset(cfg, 0, sizeof(*cfg));

    cfg->gpio_drdy = -1;
    cfg->gpio_start = -1;
    cfg->gpio_reset = -1;
    cfg->gpio_pwdn = -1;

    cfg->drdy_timeout_us = 10000;
    cfg->spi_timeout_ms = 20;
    cfg->startup_delay_ms = 150;

    cfg->rate.dr_bits = 0x06; // 250 SPS typical

    cfg->enable_internal_vref = true;
    cfg->enable_bias = true;
    cfg->enable_loff_comp = false;

    cfg->queue_len = 0;
    cfg->vref_volts = 4.5f;

    for (uint8_t i = 0; i < NOCTI_BIOPOT_NUM_CH; i++) {
        cfg->ch[i].enable = false;
        cfg->ch[i].srb2 = false;
        cfg->ch[i].mux = NOCTI_BIOPOT_MUX_SHORTED;
        cfg->ch[i].gain = NOCTI_BIOPOT_GAIN_12;
    }
}

// ============================================================
// Apply full register setup
// ============================================================

static esp_err_t apply_register_setup(void)
{
    esp_err_t err;

    err = ads1299_send_cmd(ADS1299_CMD_SDATAC);
    if (err != ESP_OK) return err;

    err = ads1299_send_cmd(ADS1299_CMD_RESET);
    if (err != ESP_OK) return err;

    nocti_timebase_busywait_us(100);

    err = ads1299_send_cmd(ADS1299_CMD_SDATAC);
    if (err != ESP_OK) return err;

    nocti_timebase_busywait_us(10);

    // slash: CONFIG1
    uint8_t c1 = 0;
    err = reg_read_u8(ADS1299_REG_CONFIG1, &c1);
    if (err != ESP_OK) return err;

    // /belajar: DR[2:0] menentukan output data rate
    // /belajar: DAISY_EN dimatikan karena tidak pakai daisy chain
    // /belajar: CLK_EN dimatikan agar clock internal tidak dipancarkan keluar
    c1 &= (uint8_t)~0x07u;
    c1 |= (uint8_t)(s.cfg.rate.dr_bits & 0x07u);
    c1 &= (uint8_t)~0x40u; // DAISY_EN=0
    c1 &= (uint8_t)~0x20u; // CLK_EN=0

    err = reg_write_u8(ADS1299_REG_CONFIG1, c1);
    if (err != ESP_OK) return err;

    // slash: CONFIG3
    uint8_t c3 = 0;
    err = reg_read_u8(ADS1299_REG_CONFIG3, &c3);
    if (err != ESP_OK) return err;

    // /belajar: reserved bits 6:5 must be 1 1 (datasheet ADS1299) :contentReference[oaicite:1]{index=1}
    c3 = (uint8_t)((c3 & (uint8_t)~0x60u) | 0x60u);

    // slash: kita tidak pakai bias measurement atau bias lead off sense
    c3 &= (uint8_t)~0x10u; // BIAS_MEAS = 0
    c3 &= (uint8_t)~0x02u; // BIAS_LOFF_SENS = 0

    // slash: referensi ADC internal atau external
    // /belajar: PD_REFBUF=1 artinya internal ref buffer ON (VREFP = AVSS + 4.5 V)
    if (s.cfg.enable_internal_vref) {
        c3 |= 0x80u;
    } else {
        c3 &= (uint8_t)~0x80u;
    }

    // slash: BIASREF pin floating pada desain NoctiNIRS, jadi pakai internal biasref
    c3 |= 0x08u; // BIASREF_INT = 1

   
    // slash: bias buffer ON atau OFF
    if (s.cfg.enable_bias) {
        c3 |= 0x04u;  // PD_BIAS = 1 -> bias buffer ON
    } else {
        c3 &= (uint8_t)~0x04u; // PD_BIAS = 0 -> bias buffer OFF
    }

    err = reg_write_u8(ADS1299_REG_CONFIG3, c3);
    if (err != ESP_OK) return err;

    // slash: CONFIG4
    // /belajar: PD_LOFF_COMP bit1, 1 = comparator enable
    uint8_t c4 = 0;
    if (s.cfg.enable_loff_comp) {
        c4 |= 0x02u;
    } else {
        c4 &= (uint8_t)~0x02u;
    }
    // SINGLE_SHOT tetap 0
    err = reg_write_u8(ADS1299_REG_CONFIG4, c4);
    if (err != ESP_OK) return err;

    // slash: channel setup
    for (uint8_t ch = 1; ch <= NOCTI_BIOPOT_NUM_CH; ch++) {
        uint8_t v = build_chset(&s.cfg.ch[ch - 1u]);
        err = reg_write_u8(ch_reg_addr(ch), v);
        if (err != ESP_OK) return err;
    }

    // slash: bias sense masks
    uint8_t bs_p = 0;
    uint8_t bs_n = 0;
    build_bias_sense(&bs_p, &bs_n);

    err = reg_write_u8(ADS1299_REG_BIAS_SENSP, bs_p);
    if (err != ESP_OK) return err;

    err = reg_write_u8(ADS1299_REG_BIAS_SENSN, bs_n);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

// ============================================================
// Public API
// ============================================================

esp_err_t nocti_biopot_init(const nocti_biopot_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (!cfg->spi) return ESP_ERR_INVALID_ARG;
    if (cfg->gpio_drdy < 0) return ESP_ERR_INVALID_ARG;

    memset(&s, 0, sizeof(s));
    s.cfg = *cfg;

    // slash: default bias override off
    s.bias_override = false;
    s.bias_sensp_override = 0;
    s.bias_sensn_override = 0;

    if (s.cfg.drdy_timeout_us == 0) s.cfg.drdy_timeout_us = 10000;
    if (s.cfg.spi_timeout_ms == 0)  s.cfg.spi_timeout_ms = 20;
    if (s.cfg.startup_delay_ms == 0) s.cfg.startup_delay_ms = 150;
    if (s.cfg.rate.dr_bits > 7) s.cfg.rate.dr_bits = 0x06;
    if (s.cfg.vref_volts <= 0.0f) s.cfg.vref_volts = 4.5f;

    // slash: normalize channel cfg supaya tidak floating
    for (uint8_t i = 0; i < NOCTI_BIOPOT_NUM_CH; i++) {
        if (!s.cfg.ch[i].enable) {
            s.cfg.ch[i].mux = NOCTI_BIOPOT_MUX_SHORTED;
        }
        if ((uint8_t)s.cfg.ch[i].mux > 7u) {
            s.cfg.ch[i].mux = NOCTI_BIOPOT_MUX_NORMAL;
        }
        if ((uint8_t)s.cfg.ch[i].gain == 0u) {
            s.cfg.ch[i].gain = NOCTI_BIOPOT_GAIN_12;
        }
    }

    if (!s.cfg.enable_internal_vref) {
        NOCTI_LOGW(TAG, "enable_internal_vref=false, pastikan external VREF benar");
    }
    if (!s.cfg.enable_bias) {
        NOCTI_LOGW(TAG, "enable_bias=false, pastikan memang sengaja");
    }

    gpio_config_t io = {0};
    io.pin_bit_mask = (1ULL << (uint32_t)s.cfg.gpio_drdy);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_NEGEDGE;
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    err = gpio_isr_handler_add(s.cfg.gpio_drdy, drdy_isr, NULL);
    if (err == ESP_ERR_INVALID_STATE) {
        NOCTI_LOGE(TAG, "GPIO ISR service belum diinstall. Panggil gpio_install_isr_service() di board atau app.");
        return err;
    }
    if (err != ESP_OK) return err;

    if (s.cfg.gpio_pwdn >= 0) {
        gpio_config_t p = {0};
        p.pin_bit_mask = (1ULL << (uint32_t)s.cfg.gpio_pwdn);
        p.mode = GPIO_MODE_OUTPUT;
        (void)gpio_config(&p);
        gpio_set_level(s.cfg.gpio_pwdn, 1);
    }

    if (s.cfg.gpio_reset >= 0) {
        gpio_config_t r = {0};
        r.pin_bit_mask = (1ULL << (uint32_t)s.cfg.gpio_reset);
        r.mode = GPIO_MODE_OUTPUT;
        (void)gpio_config(&r);
        gpio_set_level(s.cfg.gpio_reset, 1);
    }

    if (s.cfg.gpio_start >= 0) {
        gpio_config_t st = {0};
        st.pin_bit_mask = (1ULL << (uint32_t)s.cfg.gpio_start);
        st.mode = GPIO_MODE_OUTPUT;
        (void)gpio_config(&st);
        gpio_set_level(s.cfg.gpio_start, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(s.cfg.startup_delay_ms));

    // slash: hardware reset pulse (opsional)
    if (s.cfg.gpio_reset >= 0) {
        gpio_set_level(s.cfg.gpio_reset, 0);
        nocti_timebase_busywait_us(10);
        gpio_set_level(s.cfg.gpio_reset, 1);
        nocti_timebase_busywait_us(50);
    }

    err = apply_register_setup();
    if (err != ESP_OK) return err;

    if (s.cfg.queue_len > 0) {
        s.q = xQueueCreate(s.cfg.queue_len, sizeof(nocti_biopot_sample_t));
        if (!s.q) return ESP_ERR_NO_MEM;
    }

    // slash: pastikan notify bersih saat init
    (void)ulTaskNotifyTake(pdTRUE, 0);
    wait_task_clear();

    s.inited = true;
    s.streaming = false;

    NOCTI_LOGI(TAG, "init ok, queue=%s", s.q ? "on" : "off");
    return ESP_OK;
}

esp_err_t nocti_biopot_deinit(void)
{
    if (!s.inited) return ESP_OK;

    (void)nocti_biopot_stop();
    (void)gpio_isr_handler_remove(s.cfg.gpio_drdy);

    if (s.q) {
        vQueueDelete(s.q);
        s.q = NULL;
    }

    memset(&s, 0, sizeof(s));
    return ESP_OK;
}

QueueHandle_t nocti_biopot_get_queue(void)
{
    return s.q;
}

bool nocti_biopot_is_streaming(void)
{
    return s.streaming;
}

esp_err_t nocti_biopot_start(void)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (s.streaming) return ESP_OK;

    // slash: bersihkan state wait dan notify sebelum start
    (void)ulTaskNotifyTake(pdTRUE, 0);
    wait_task_clear();

    esp_err_t err = ads1299_send_cmd(ADS1299_CMD_SDATAC);
    if (err != ESP_OK) return err;

    err = ads1299_send_cmd(ADS1299_CMD_START);
    if (err != ESP_OK) return err;

    err = ads1299_send_cmd(ADS1299_CMD_RDATAC);
    if (err != ESP_OK) return err;

    s.streaming = true;
    return ESP_OK;
}

esp_err_t nocti_biopot_stop(void)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (!s.streaming) return ESP_OK;

    // slash: kalau ada task yang sedang menunggu DRDY, bangunkan dulu
    taskENTER_CRITICAL(&s_wait_mux);
    TaskHandle_t t = (TaskHandle_t)s.wait_task;
    s.wait_task = NULL;
    taskEXIT_CRITICAL(&s_wait_mux);
    if (t) {
        xTaskNotifyGive(t);
    }

    esp_err_t err = ads1299_send_cmd(ADS1299_CMD_SDATAC);
    if (err != ESP_OK) return err;

    err = ads1299_send_cmd(ADS1299_CMD_STOP);
    if (err != ESP_OK) return err;

    // slash: drain notify setelah stop
    (void)ulTaskNotifyTake(pdTRUE, 0);

    s.streaming = false;
    return ESP_OK;
}

esp_err_t nocti_biopot_set_bias_sense_override(bool enable, uint8_t sensp_mask, uint8_t sensn_mask)
{
    esp_err_t err = ensure_idle();
    if (err != ESP_OK) return err;

    // slash: update state
    s.bias_override = enable;
    s.bias_sensp_override = sensp_mask;
    s.bias_sensn_override = sensn_mask;

    // slash: apply ke chip
    err = ads1299_send_cmd(ADS1299_CMD_SDATAC);
    if (err != ESP_OK) return err;

    uint8_t bs_p = 0;
    uint8_t bs_n = 0;
    build_bias_sense(&bs_p, &bs_n);

    err = reg_write_u8(ADS1299_REG_BIAS_SENSP, bs_p);
    if (err != ESP_OK) return err;

    err = reg_write_u8(ADS1299_REG_BIAS_SENSN, bs_n);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t nocti_biopot_read_sample(nocti_biopot_sample_t *out_opt)
{
    if (!out_opt) return ESP_ERR_INVALID_ARG;
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (!s.streaming) return ESP_ERR_INVALID_STATE;

    esp_err_t err = wait_drdy_low(s.cfg.drdy_timeout_us);
    if (err != ESP_OK) return err;

    nocti_time_us_t t_us = nocti_timebase_now_us();

    memset(s.frame_tx, 0, sizeof(s.frame_tx));
    err = spi_xfer(s.frame_tx, s.frame_rx, NOCTI_BIOPOT_FRAME_BYTES);
    if (err != ESP_OK) return err;

    nocti_biopot_sample_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_us = t_us;

    rec.status24 = u24_be(&s.frame_rx[0]);

    for (uint8_t i = 0; i < NOCTI_BIOPOT_NUM_CH; i++) {
        uint32_t v24 = u24_be(&s.frame_rx[NOCTI_BIOPOT_STATUS_BYTES + (i * 3u)]);
        rec.ch[i] = s24_to_s32(v24);
    }

    *out_opt = rec;

    if (s.q) {
        (void)xQueueSend(s.q, &rec, 0);
    }

    return ESP_OK;
}

// ============================================================
// Runtime config guard
// ============================================================

static esp_err_t ensure_idle(void)
{
    if (!s.inited) return ESP_ERR_INVALID_STATE;
    if (s.streaming) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t nocti_biopot_apply_channel_cfg(uint8_t ch1_to_8, const nocti_biopot_ch_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (ch1_to_8 < 1 || ch1_to_8 > NOCTI_BIOPOT_NUM_CH) return ESP_ERR_INVALID_ARG;

    esp_err_t err = ensure_idle();
    if (err != ESP_OK) return err;

    s.cfg.ch[ch1_to_8 - 1u] = *cfg;

    err = ads1299_send_cmd(ADS1299_CMD_SDATAC);
    if (err != ESP_OK) return err;

    uint8_t v = build_chset(cfg);
    err = reg_write_u8(ch_reg_addr(ch1_to_8), v);
    if (err != ESP_OK) return err;

    uint8_t bs_p = 0;
    uint8_t bs_n = 0;
    build_bias_sense(&bs_p, &bs_n);

    err = reg_write_u8(ADS1299_REG_BIAS_SENSP, bs_p);
    if (err != ESP_OK) return err;

    err = reg_write_u8(ADS1299_REG_BIAS_SENSN, bs_n);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t nocti_biopot_set_channel_enable(uint8_t ch1_to_8, bool enable)
{
    if (ch1_to_8 < 1 || ch1_to_8 > NOCTI_BIOPOT_NUM_CH) return ESP_ERR_INVALID_ARG;

    nocti_biopot_ch_cfg_t c = s.cfg.ch[ch1_to_8 - 1u];
    c.enable = enable;

    if (!enable) c.mux = NOCTI_BIOPOT_MUX_SHORTED;

    return nocti_biopot_apply_channel_cfg(ch1_to_8, &c);
}

esp_err_t nocti_biopot_set_channel_gain(uint8_t ch1_to_8, nocti_biopot_gain_t gain)
{
    if (ch1_to_8 < 1 || ch1_to_8 > NOCTI_BIOPOT_NUM_CH) return ESP_ERR_INVALID_ARG;

    nocti_biopot_ch_cfg_t c = s.cfg.ch[ch1_to_8 - 1u];
    c.gain = gain;

    return nocti_biopot_apply_channel_cfg(ch1_to_8, &c);
}

// ============================================================
// Impedance helpers
// ============================================================

static uint8_t build_loff_reg(nocti_biopot_loff_i_t i_mag, nocti_biopot_loff_f_t f_sel)
{
    // slash: comp_th bit5 default enable threshold level setting
    uint8_t comp_th = (uint8_t)(1u << 5);
    uint8_t i_bits = (uint8_t)((i_mag & 0x03u) << 2);

    uint8_t f_bits = 0;
    if (f_sel == NOCTI_BIOPOT_LOFF_F_DC) f_bits = 0;
    if (f_sel == NOCTI_BIOPOT_LOFF_F_AC_7P8) f_bits = 1;
    if (f_sel == NOCTI_BIOPOT_LOFF_F_AC_31P2) f_bits = 2;

    // /belajar: VLEAD_OFF_EN (bit4) harus = 1 untuk mengaktifkan AC voltage excitation.
    // Tanpa bit ini, mode AC_7P8 / AC_31P2 tidak menginjeksikan sinyal AC ke elektroda
    // dan Goertzel tidak mengukur impedansi yang bermakna.
    uint8_t vlead_en = (f_sel != NOCTI_BIOPOT_LOFF_F_DC) ? (uint8_t)(1u << 4) : 0u;

    return (uint8_t)(comp_th | vlead_en | i_bits | (f_bits & 0x03u));
}

static float lead_off_current_amp(nocti_biopot_loff_i_t i_mag)
{
    if (i_mag == NOCTI_BIOPOT_LOFF_I_6NA)  return 6e-9f;
    if (i_mag == NOCTI_BIOPOT_LOFF_I_24NA) return 24e-9f;
    if (i_mag == NOCTI_BIOPOT_LOFF_I_6UA)  return 6e-6f;
    return 24e-6f;
}

typedef struct {
    float q1;
    float q2;
} goertzel_t;

static void goertzel_reset(goertzel_t *g)
{
    g->q1 = 0.0f;
    g->q2 = 0.0f;
}

static void goertzel_step(goertzel_t *g, float x, float coeff)
{
    float q0 = (coeff * g->q1) - g->q2 + x;
    g->q2 = g->q1;
    g->q1 = q0;
}

static float goertzel_mag(const goertzel_t *g, float coeff)
{
    float p = (g->q1 * g->q1) + (g->q2 * g->q2) - (coeff * g->q1 * g->q2);
    if (p < 0.0f) p = 0.0f;
    return sqrtf(p);
}

// ============================================================
// Impedance check
// ============================================================

esp_err_t nocti_biopot_impedance_check(const nocti_biopot_imp_cfg_t *cfg,
                                      nocti_biopot_imp_result_t *out_opt)
{
    if (!cfg || !out_opt) return ESP_ERR_INVALID_ARG;

    esp_err_t err = ensure_idle();
    if (err != ESP_OK) return err;

    nocti_biopot_imp_cfg_t c = *cfg;

    if (c.n_samples == 0) c.n_samples = 250;
    if (c.fs_hz <= 0.0f) c.fs_hz = 250.0f;

    uint8_t reg_loff = 0;
    uint8_t reg_sensp = 0;
    uint8_t reg_sensn = 0;
    uint8_t reg_c4 = 0;

    err = ads1299_send_cmd(ADS1299_CMD_SDATAC);
    if (err != ESP_OK) return err;

    err = reg_read_u8(ADS1299_REG_LOFF, &reg_loff);
    if (err != ESP_OK) return err;

    err = reg_read_u8(ADS1299_REG_LOFF_SENSP, &reg_sensp);
    if (err != ESP_OK) return err;

    err = reg_read_u8(ADS1299_REG_LOFF_SENSN, &reg_sensn);
    if (err != ESP_OK) return err;

    err = reg_read_u8(ADS1299_REG_CONFIG4, &reg_c4);
    if (err != ESP_OK) return err;

    uint8_t loff_new = build_loff_reg(c.i_mag, c.f_sel);
    err = reg_write_u8(ADS1299_REG_LOFF, loff_new);
    if (err != ESP_OK) goto restore;

    err = reg_write_u8(ADS1299_REG_LOFF_SENSP, c.loff_sensp_mask);
    if (err != ESP_OK) goto restore;

    err = reg_write_u8(ADS1299_REG_LOFF_SENSN, c.loff_sensn_mask);
    if (err != ESP_OK) goto restore;

    // slash: disable comparator agar tidak ganggu pembacaan amplitude
    uint8_t c4_new = (uint8_t)(reg_c4 & (uint8_t)~0x02u);
    err = reg_write_u8(ADS1299_REG_CONFIG4, c4_new);
    if (err != ESP_OK) goto restore;

    err = ads1299_send_cmd(ADS1299_CMD_START);
    if (err != ESP_OK) goto restore;

    err = ads1299_send_cmd(ADS1299_CMD_RDATAC);
    if (err != ESP_OK) goto restore;

    float f0 = 0.0f;
    if (c.f_sel == NOCTI_BIOPOT_LOFF_F_AC_7P8)  f0 = 7.8f;
    if (c.f_sel == NOCTI_BIOPOT_LOFF_F_AC_31P2) f0 = 31.2f;

    const bool do_goertzel = (f0 > 0.0f);
    float coeff = 0.0f;

    if (do_goertzel) {
        float w = (2.0f * 3.1415926f * f0) / c.fs_hz;
        coeff = 2.0f * cosf(w);
    } else {
        NOCTI_LOGW(TAG, "impedance_check DC: skip amplitude estimate, z_ohm diisi 0");
    }

    goertzel_t g[NOCTI_BIOPOT_NUM_CH];
    for (uint8_t i = 0; i < NOCTI_BIOPOT_NUM_CH; i++) {
        goertzel_reset(&g[i]);
    }

    uint16_t discard = 10;

    memset(out_opt, 0, sizeof(*out_opt));

    for (uint16_t n = 0; n < (uint16_t)(c.n_samples + discard); n++) {
        err = wait_drdy_low(s.cfg.drdy_timeout_us);
        if (err != ESP_OK) goto stop_temp;

        memset(s.frame_tx, 0, sizeof(s.frame_tx));
        err = spi_xfer(s.frame_tx, s.frame_rx, NOCTI_BIOPOT_FRAME_BYTES);
        if (err != ESP_OK) goto stop_temp;

        if (n < discard) continue;
        if (!do_goertzel) continue;

        for (uint8_t ch = 0; ch < NOCTI_BIOPOT_NUM_CH; ch++) {
            uint32_t v24 = u24_be(&s.frame_rx[NOCTI_BIOPOT_STATUS_BYTES + (ch * 3u)]);
            int32_t x = s24_to_s32(v24);
            goertzel_step(&g[ch], (float)x, coeff);
        }
    }

    if (c.read_loff_status) {
        uint8_t sp = 0;
        uint8_t sn = 0;
        (void)reg_read_u8(ADS1299_REG_LOFF_STATP, &sp);
        (void)reg_read_u8(ADS1299_REG_LOFF_STATN, &sn);
        out_opt->loff_statp = sp;
        out_opt->loff_statn = sn;
    }

    if (do_goertzel) {
        const float fullscale = 8388607.0f;
        float i_amp = lead_off_current_amp(c.i_mag);

        for (uint8_t ch = 0; ch < NOCTI_BIOPOT_NUM_CH; ch++) {
            float mag = goertzel_mag(&g[ch], coeff);
            float amp_counts = (c.n_samples > 0) ? (2.0f * mag / (float)c.n_samples) : 0.0f;

            float gain = gain_to_float(s.cfg.ch[ch].gain);
            float v_amp = 0.0f;
            if (gain > 0.0f) {
                v_amp = (amp_counts / fullscale) * (s.cfg.vref_volts / gain);
            }

            float z = 0.0f;
            if (i_amp > 0.0f) z = v_amp / i_amp;
            out_opt->z_ohm[ch] = z;
        }
    } else {
        for (uint8_t ch = 0; ch < NOCTI_BIOPOT_NUM_CH; ch++) {
            out_opt->z_ohm[ch] = 0.0f;
        }
    }

stop_temp:
    (void)ads1299_send_cmd(ADS1299_CMD_SDATAC);
    (void)ads1299_send_cmd(ADS1299_CMD_STOP);

restore:
    (void)ads1299_send_cmd(ADS1299_CMD_SDATAC);
    (void)reg_write_u8(ADS1299_REG_LOFF, reg_loff);
    (void)reg_write_u8(ADS1299_REG_LOFF_SENSP, reg_sensp);
    (void)reg_write_u8(ADS1299_REG_LOFF_SENSN, reg_sensn);
    (void)reg_write_u8(ADS1299_REG_CONFIG4, reg_c4);

    return err;
}