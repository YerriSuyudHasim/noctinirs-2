/* Composition adapter: ADC owner retains SPI/reset, LED owner retains I2C/timer.
 * This task serializes ADC-port calls and owns pure pairing policy. */
#include "sdkconfig.h"
#include "nn2_acquisition_esp.h"
#include "nn2_led_bringup.h"
#include "nn2_adapter_ads131.h"
#include "nn2_board_v1.h"
#include "nn2_rtos_config.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#ifndef CONFIG_NN2_ADC_V4_DARK_BASELINE
#define CONFIG_NN2_ADC_V4_DARK_BASELINE 0
#endif
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t task;
static nn2_port_adc_stream_t adc;
static nn2_port_optical_t led;
static nn2_port_safety_t led_off;
static QueueHandle_t windows, frames;
static nn2_core_acquisition_t policy;
typedef struct { uint64_t timestamp; uint8_t payload[NN2_ACQ_PAYLOAD_SIZE]; } frame_t;
static frame_t built;
static nn2_port_optical_config_t config;
static uint8_t lifecycle = NN2_OPTICAL_IDLE;
static uint32_t generation, dropped;
static bool wanted, queue_fault, stopped = true;
static nn2_acquisition_status_t cached;
nn2_acquisition_status_t nn2_acquisition_status(void)
{
    portENTER_CRITICAL(&lock);
    nn2_acquisition_status_t v=cached; v.stop_ack=stopped;
    portEXIT_CRITICAL(&lock); return v;
}
static void observe(void)
{
    nn2_core_adc_observation_t v;
    if (adc.observe(adc.ctx,&v,100u)==NN2_OK) {
        portENTER_CRITICAL(&lock);
        cached.adc=v; cached.dropped=dropped;
        portEXIT_CRITICAL(&lock);
    }
}
static nn2_status_t cancel(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&lock);
    wanted=false; ++generation;
    /* Same lock as permission + LED start; no start may overtake safe-off. */
    nn2_status_t result=led_off.hold_off(led_off.ctx);
    if (lifecycle!=NN2_OPTICAL_IDLE && lifecycle!=NN2_OPTICAL_FAULT) lifecycle=NN2_OPTICAL_STOPPING;
    portEXIT_CRITICAL(&lock);
    return result;
}
static void fail(void)
{
    (void)led_off.hold_off(led_off.ctx);
    portENTER_CRITICAL(&lock);
    wanted=false; lifecycle=NN2_OPTICAL_FAULT;
    portEXIT_CRITICAL(&lock);
}
static bool current(uint32_t epoch)
{
    portENTER_CRITICAL(&lock);
    bool ok=wanted && epoch==generation && !queue_fault;
    portEXIT_CRITICAL(&lock);
    return ok;
}
static nn2_status_t prepare_led(const nn2_port_optical_config_t *cfg, uint32_t epoch)
{
    portENTER_CRITICAL(&lock);
    nn2_status_t result = wanted && epoch == generation && !queue_fault
        ? nn2_led_bringup_prepare(cfg) : NN2_ERR_INVALID_STATE;
    portEXIT_CRITICAL(&lock);
    if (result != NN2_OK) return result;
    uint64_t end = (uint64_t)esp_timer_get_time() + 1000000u;
    while (current(epoch) && (uint64_t)esp_timer_get_time() < end) {
        if (nn2_led_bringup_prepared()) return NN2_OK;
        if (led.state(led.ctx) == NN2_OPTICAL_FAULT) return NN2_ERR_IO;
        vTaskDelay(1u); /* ADC is stopped throughout LED hardware preparation. */
    }
    return NN2_ERR_TIMEOUT;
}
static void worker(void *ctx)
{
    (void)ctx;
    nn2_status_t created=nn2_adapter_ads131_create(&nn2_board_v1_adc,&adc);
    if (created!=NN2_OK) { portENTER_CRITICAL(&lock); stopped=false; portEXIT_CRITICAL(&lock); fail(); vTaskDelete(NULL); return; }
    bool live=false;
    uint32_t epoch=0;
    uint64_t observed_us=0u;
    for (;;) {
        portENTER_CRITICAL(&lock);
        bool run=wanted, overflow=queue_fault;
        uint32_t next=generation;
        nn2_port_optical_config_t snapshot=config;
        portEXIT_CRITICAL(&lock);
        if (overflow) { fail(); run=false; }
        if (live && (!run || next!=epoch ||
            (!CONFIG_NN2_ADC_V4_DARK_BASELINE && led.state(led.ctx)==NN2_OPTICAL_FAULT))) {
            if (!CONFIG_NN2_ADC_V4_DARK_BASELINE && led.state(led.ctx)==NN2_OPTICAL_FAULT) fail();
            nn2_status_t result=adc.stop(adc.ctx,100u);
            live=false;
            if (result!=NN2_OK) fail();
            portENTER_CRITICAL(&lock);
            stopped=result==NN2_OK;
            portEXIT_CRITICAL(&lock);
            dropped+=(uint32_t)uxQueueMessagesWaiting(frames);
            xQueueReset(frames);
            portENTER_CRITICAL(&lock);
            if (policy.active) ++cached.discarded_tail;
            portEXIT_CRITICAL(&lock);
            nn2_core_acquisition_reset(&policy);
            observe();
            if (result==NN2_OK) nn2_led_bringup_report_timing();
        }
        if (!live && run && next!=epoch) {
            epoch=next;
            xQueueReset(windows); xQueueReset(frames);
            nn2_core_acquisition_reset(&policy); dropped=0;
            portENTER_CRITICAL(&lock); stopped=false; portEXIT_CRITICAL(&lock);
            nn2_status_t result = CONFIG_NN2_ADC_V4_DARK_BASELINE
                ? led_off.hold_off(led_off.ctx) : prepare_led(&snapshot, epoch);
            if (result==NN2_OK && current(epoch)) result=adc.init(adc.ctx,100u);
            else result=NN2_ERR_INVALID_STATE;
            if (result==NN2_OK && current(epoch)) result=adc.start(adc.ctx,500u);
            if (result==NN2_OK && current(epoch) && !CONFIG_NN2_ADC_V4_DARK_BASELINE) {
                /* Activation queues only the preconfigured timer start.
                 * Cancellation uses the same acquisition -> LED lock order. */
                portENTER_CRITICAL(&lock);
                if (wanted && epoch==generation && !queue_fault)
                    result=nn2_led_bringup_activate();
                else result=NN2_ERR_INVALID_STATE;
                portEXIT_CRITICAL(&lock);
            }
            live=true; /* Even a failed attempt requires acknowledged STOP. */
            if (result!=NN2_OK) fail();
            else {
                portENTER_CRITICAL(&lock);
                if (wanted && epoch==generation) lifecycle=NN2_OPTICAL_RUNNING;
                portEXIT_CRITICAL(&lock);
            }
        }
        if (live && current(epoch)) {
            nn2_core_adc_record_t raw;
            nn2_status_t result=adc.read(adc.ctx,&raw,20u);
            uint64_t now = (uint64_t)esp_timer_get_time();
            if (now - observed_us >= 100000u) { observe(); observed_us=now; }
            if (CONFIG_NN2_ADC_V4_DARK_BASELINE) {
                if (result!=NN2_OK && result!=NN2_ERR_EMPTY && result!=NN2_ERR_TIMEOUT) fail();
                continue; /* Drain real ADC records, publish no optical measurements. */
            }
            nn2_core_acq_window_t w;
            while (xQueueReceive(windows,&w,0)==pdTRUE)
                if (nn2_core_acquisition_window(&policy,&w)!=NN2_OK) fail();
            if (result==NN2_OK && current(epoch)) {
                result=nn2_core_acquisition_sample(&policy,&raw,built.payload);
                if (result==NN2_OK) {
                    built.timestamp=nn2_core_get_u64(built.payload+12);
                    nn2_core_put_u32(built.payload+32,dropped);
                    if (xQueueSend(frames,&built,0)!=pdTRUE) ++dropped;
                } else if (result!=NN2_ERR_EMPTY) fail();
            } else if (result!=NN2_ERR_EMPTY && result!=NN2_ERR_TIMEOUT) fail();
        } else {
            portENTER_CRITICAL(&lock);
            bool can_idle=stopped && lifecycle==NN2_OPTICAL_STOPPING;
            portEXIT_CRITICAL(&lock);
            if (can_idle && led.state(led.ctx)==NN2_OPTICAL_IDLE) {
                portENTER_CRITICAL(&lock);
                if (!wanted && stopped) lifecycle=NN2_OPTICAL_IDLE;
                portEXIT_CRITICAL(&lock);
            }
            vTaskDelay(1u);
        }
    }
}
bool nn2_acquisition_window(const nn2_core_acq_window_t *w)
{
    if (!windows || xQueueSend(windows,w,0)!=pdTRUE) {
        portENTER_CRITICAL(&lock); queue_fault=true; portEXIT_CRITICAL(&lock);
        return false;
    }
    return true;
}
static nn2_status_t start(void *ctx, const nn2_port_optical_config_t *cfg)
{
    (void)ctx;
    if (!cfg || !nn2_core_acquisition_fs_valid(cfg->fs)) return NN2_ERR_INVALID_ARG;
    portENTER_CRITICAL(&lock);
    bool ok=lifecycle==NN2_OPTICAL_IDLE && stopped;
    if (ok) { config=*cfg; wanted=true; queue_fault=false; ++generation; lifecycle=NN2_OPTICAL_STARTING; }
    portEXIT_CRITICAL(&lock);
    return ok?NN2_OK:NN2_ERR_INVALID_STATE;
}
static uint8_t state(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&lock); uint8_t state=lifecycle; portEXIT_CRITICAL(&lock);
    if (state==NN2_OPTICAL_RUNNING) {
        if (CONFIG_NN2_ADC_V4_DARK_BASELINE) return state;
        uint8_t s=led.state(led.ctx);
        return s==NN2_OPTICAL_RUNNING?state:s==NN2_OPTICAL_FAULT?s:NN2_OPTICAL_STARTING;
    }
    return state;
}
static nn2_status_t clear(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&lock); bool ok=stopped && !wanted; portEXIT_CRITICAL(&lock);
    if (!ok || led.clear_fault(led.ctx)!=NN2_OK) return NN2_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock); lifecycle=NN2_OPTICAL_IDLE; queue_fault=false; portEXIT_CRITICAL(&lock);
    return NN2_OK;
}
static nn2_status_t read_frame(void *ctx, uint8_t *out, size_t capacity, uint64_t *timestamp)
{
    (void)ctx;
    frame_t f;
    if (!out || !timestamp || capacity<sizeof(f.payload)) return NN2_ERR_INVALID_ARG;
    if (state(NULL)!=NN2_OPTICAL_RUNNING || xQueueReceive(frames,&f,0)!=pdTRUE) return NN2_ERR_EMPTY;
    memcpy(out,f.payload,sizeof(f.payload)); *timestamp=f.timestamp;
    return NN2_OK;
}
esp_err_t nn2_acquisition_init(void)
{
    if (task) return ESP_ERR_INVALID_STATE;
    led=nn2_led_bringup_port(); led_off=nn2_led_bringup_safety_port();
    windows=xQueueCreate(64,sizeof(nn2_core_acq_window_t));
    frames=xQueueCreate(4,sizeof(frame_t));
    if (!windows || !frames) {
        if (windows) { vQueueDelete(windows); windows=NULL; }
        if (frames) { vQueueDelete(frames); frames=NULL; }
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(worker,"nn2_pair",6144,NULL,NN2_RTOS_ADS131_CONSUMER_PRIORITY,&task,NN2_RTOS_ADS131_CONSUMER_CORE)!=pdPASS) {
        vQueueDelete(windows); vQueueDelete(frames); windows=frames=NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
nn2_port_optical_t nn2_acquisition_port(void)
{
    return (nn2_port_optical_t){.start=start,.state=state,.clear_fault=clear,
        .current_limit_ma=30,.read_frame=read_frame,.real_samples=true,
        .policy_info=CONFIG_NN2_ADC_V4_DARK_BASELINE ?
        "NoctiNIRS2/0.1.0;wire=ADR0004/1;led=OFF;baseline=V4_DARK;frames=unsupported;adc=ADS131M04;fs_min=2;fs_max=10;limit_s=0" :
        CONFIG_NN2_LED_SHORT_BENCH ?
        "NoctiNIRS2/0.1.0;wire=ADR0004/1;led=REAL_BRINGUP;baseline=V4_PIPELINE;short_policy=BENCH_WARN;frames=ACQ1;adc=ADS131M04;fs_min=2;fs_max=10;limit_s=0;k=10/8" :
        "NoctiNIRS2/0.1.0;wire=ADR0004/1;led=REAL_BRINGUP;baseline=V4_PIPELINE;short_policy=STRICT;frames=ACQ1;adc=ADS131M04;fs_min=2;fs_max=10;k=10/8"};
}
nn2_port_safety_t nn2_acquisition_safety_port(void)
{ return (nn2_port_safety_t){.hold_off=cancel}; }
