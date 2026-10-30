/* Communication-only binding. All acquisition/timing stays in V4 components.
 * App/UDP have one Core0 owner. A Core1 worker serializes V4 start/stop and
 * resource allocation. Neither a UDP callback nor the ADC task performs
 * blocking I2C. There is no USB command console. */
#if CONFIG_NOCTI_V4_NN2_COMMUNICATION
#include "nn2_app_comm.h"
#include "nn2_net.h"
#include "nn2_wifi.h"
#include "nn2_config.h"
#include "esp_random.h"
#include "esp_timer.h"
static nn2_app_comm_storage_t s_nn2_app;
static TaskHandle_t s_control_worker, s_control_network;
static portMUX_TYPE s_control_lock=portMUX_INITIALIZER_UNLOCKED;
static nn2_port_optical_config_t s_control_config;
static uint32_t s_control_generation, s_control_start_epoch;
static uint8_t s_control_state=NN2_OPTICAL_IDLE;
static bool s_control_wanted, s_control_working, s_control_stop, s_control_fault;
static bool s_control_clean=true;
static esp_err_t s_control_error=ESP_OK;
static const char *s_boot_stage="not_started";
static void control_set_boot_stage(const char *name)
{
    if (name!=NULL && name[0]!='\0')
        __atomic_store_n(&s_boot_stage, name, __ATOMIC_RELEASE);
}
static const char *control_boot_stage(void)
{
    const char *stage=__atomic_load_n(&s_boot_stage, __ATOMIC_ACQUIRE);
    return stage!=NULL ? stage : "not_started";
}
/* Sticky. CLEAR cannot hide a boot failure, and START requires IDLE. */
static void control_note_boot_fault(void)
{
    portENTER_CRITICAL(&s_control_lock);
    s_control_fault=true;
    s_control_error=ESP_FAIL;
    if (s_control_state!=NN2_OPTICAL_RUNNING) s_control_state=NN2_OPTICAL_FAULT;
    portEXIT_CRITICAL(&s_control_lock);
}
static bool control_start_allowed(void)
{
    portENTER_CRITICAL(&s_control_lock);
    bool ok=s_control_wanted && s_control_start_epoch==s_control_generation;
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}
static nn2_status_t control_off(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_control_lock);
    s_control_wanted=false; ++s_control_generation;
    if (s_control_working || !s_control_clean) {
        s_control_stop=true;
        if (!s_control_fault) s_control_state=NN2_OPTICAL_STOPPING;
    }
    /* Same gate as PCA release: a cancelled startup cannot release RESET. */
    esp_err_t result=nocti_v4_board_set_emitter_gate(&s_board,false);
    nocti_v4_async_led_cancel_submissions(&s_async_led);
    portEXIT_CRITICAL(&s_control_lock);
    if (s_control_worker) xTaskNotifyGive(s_control_worker);
    return result==ESP_OK ? NN2_OK : NN2_ERR_IO;
}
/* Called only after scheduler STOP and optical/ADC quiescence in scan_stop.
 * Opening reset here permits only synchronous all-OFF restore/readback; the
 * scheduler is already stopped and the async queue is drained by V4 exit. */
static void control_allow_safe_restore(void)
{
    portENTER_CRITICAL(&s_control_lock);
    if (!s_control_wanted) (void)nocti_v4_board_set_emitter_gate(&s_board,true);
    portEXIT_CRITICAL(&s_control_lock);
}
static nn2_status_t control_start(void *ctx,const nn2_port_optical_config_t *cfg)
{
    (void)ctx;
    if (!cfg || !(cfg->fs==2 || cfg->fs==4 || cfg->fs==5 || cfg->fs==8 || cfg->fs==10))
        return NN2_ERR_INVALID_ARG;
    for (unsigned i=0u;i<20u;++i) if (cfg->current_ma[i]>30u) return NN2_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_control_lock);
    bool ok=s_control_state==NN2_OPTICAL_IDLE && !s_control_working && !s_control_stop;
    if (ok) {
        s_control_config=*cfg; s_control_wanted=true; ++s_control_generation;
        s_control_state=NN2_OPTICAL_STARTING; s_control_clean=false;
    }
    portEXIT_CRITICAL(&s_control_lock);
    if (ok) xTaskNotifyGive(s_control_worker);
    return ok ? NN2_OK : NN2_ERR_INVALID_STATE;
}
static uint8_t control_state(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_control_lock); uint8_t v=s_control_state; portEXIT_CRITICAL(&s_control_lock);
    return v;
}
static nn2_status_t control_clear(void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_control_lock);
    /* A hardware fault needs a reboot/review; protocol CLEAR never hides it. */
    bool ok=!s_control_fault && !s_control_working && !s_control_stop && !s_control_wanted;
    if (ok) s_control_state=NN2_OPTICAL_IDLE;
    portEXIT_CRITICAL(&s_control_lock);
    return ok ? NN2_OK : NN2_ERR_INVALID_STATE;
}
static void control_worker(void *ctx)
{
    (void)ctx;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5u));
        portENTER_CRITICAL(&s_control_lock);
        bool start=s_control_wanted && s_control_state==NN2_OPTICAL_STARTING;
        nn2_port_optical_config_t cfg=s_control_config;
        if (start) { s_control_working=true; s_control_start_epoch=s_control_generation; }
        portEXIT_CRITICAL(&s_control_lock);
        if (start) {
            portENTER_CRITICAL(&s_control_lock);
            esp_err_t result=s_control_wanted && s_control_start_epoch==s_control_generation ?
                nocti_v4_board_set_emitter_gate(&s_board,true) : ESP_ERR_INVALID_STATE;
            portEXIT_CRITICAL(&s_control_lock);
            if (result==ESP_OK) result=nocti_v4_led_reinit(&s_led);
            for (uint8_t i=0u;i<20u && result==ESP_OK;++i)
                result=control_start_allowed() ? nocti_v4_led_set_iref_ma(&s_led,i,cfg.current_ma[i]) : ESP_ERR_INVALID_STATE;
            if (result==ESP_OK && control_start_allowed())
                result=nocti_v4_led_run_fast_i2c_qualification(&s_led);
            if (result==ESP_OK && control_start_allowed())
                result=nocti_v4_scheduler_set_fs(&s_scheduler,cfg.fs);
            if (result==ESP_OK && control_start_allowed()) result=scan_start();
            portENTER_CRITICAL(&s_control_lock);
            bool cancelled=!s_control_wanted || s_control_start_epoch!=s_control_generation;
            if (!cancelled && result==ESP_OK) s_control_state=NN2_OPTICAL_RUNNING;
            else {
                if (!cancelled) { s_control_fault=true; s_control_error=result; }
                s_control_wanted=false; s_control_stop=true;
                s_control_state=s_control_fault ? NN2_OPTICAL_FAULT : NN2_OPTICAL_STOPPING;
                (void)nocti_v4_board_set_emitter_gate(&s_board,false);
            }
            s_control_working=false;
            portEXIT_CRITICAL(&s_control_lock);
        }
        if (control_state(NULL)==NN2_OPTICAL_RUNNING) {
            nocti_v4_scheduler_status_t sched;
            nocti_v4_ads_status_snapshot_t adc;
            nocti_v4_scheduler_get_status(&s_scheduler,&sched);
            nocti_v4_ads_get_status(&s_ads,&adc);
            if (sched.lifecycle==NOCTI_V4_SCAN_FAULTED || adc.lifecycle==NOCTI_V4_ADS_FAULTED) {
                portENTER_CRITICAL(&s_control_lock); s_control_fault=true; portEXIT_CRITICAL(&s_control_lock);
                (void)control_off(NULL);
                portENTER_CRITICAL(&s_control_lock); s_control_state=NN2_OPTICAL_FAULT; portEXIT_CRITICAL(&s_control_lock);
            }
        }
        portENTER_CRITICAL(&s_control_lock);
        bool stop=s_control_stop;
        if (stop) { s_control_stop=false; s_control_working=true; }
        portEXIT_CRITICAL(&s_control_lock);
        if (stop) {
            esp_err_t result=scan_stop();
            portENTER_CRITICAL(&s_control_lock);
            (void)nocti_v4_board_set_emitter_gate(&s_board,false);
            if (result!=ESP_OK) { s_control_fault=true; s_control_error=result; }
            s_control_working=false; s_control_clean=result==ESP_OK;
            s_control_state=s_control_fault ? NN2_OPTICAL_FAULT : NN2_OPTICAL_IDLE;
            portEXIT_CRITICAL(&s_control_lock);
        }
    }
}
static uint64_t control_now(void *ctx) { (void)ctx; return (uint64_t)esp_timer_get_time(); }
static void control_network(void *ctx)
{
    (void)ctx;
    for (;;) {
        nn2_wifi_status_t wifi=nn2_wifi_get_status();
        if (nn2_net_poll(wifi.connected,wifi.link_epoch)!=NN2_OK) (void)control_off(NULL);
        vTaskDelay(pdMS_TO_TICKS(1u));
    }
}
#define HLOAD(field) __atomic_load_n(&s_health.field, __ATOMIC_RELAXED)
static size_t control_read_diag(void *ctx, uint8_t kind, uint8_t *out, size_t cap)
{
    (void)ctx;
    if (!out || cap < 32u) return 0u;
    nocti_v4_task_wdt_log_pending();
    __atomic_store_n(&s_health.task_wdt_subscribe_errors,
                     nocti_v4_task_wdt_subscribe_errors(), __ATOMIC_RELAXED);
    __atomic_store_n(&s_health.task_wdt_kick_errors,
                     nocti_v4_task_wdt_kick_errors(), __ATOMIC_RELAXED);
    nocti_v4_ads_sample_stack_watermarks(&s_ads);
    nocti_v4_async_led_sample_stack_watermark(&s_async_led);
    nocti_v4_scheduler_sample_stack_watermark(&s_scheduler);
    nocti_v4_optical_sample_stack_watermarks(&s_optical);
    nocti_v4_ads_status_snapshot_t adc;
    nocti_v4_scheduler_status_t sched;
    nocti_v4_ads_get_status(&s_ads, &adc);
    nocti_v4_scheduler_get_status(&s_scheduler, &sched);
    const uint32_t ctrl_hwm = s_control_worker ?
        (uint32_t)uxTaskGetStackHighWaterMark(s_control_worker) : 0u;
    const uint32_t net_hwm = s_control_network ?
        (uint32_t)uxTaskGetStackHighWaterMark(s_control_network) : 0u;
    int wrote;
    if (kind == 0u) {
        wrote = snprintf((char *)out, cap,
            "v4health=1 boot_stage=%s boot82_bound=0 adc_missed=%" PRIu32
            " adc_backlog_catchup=%" PRIu32 " adc_missed_backlog=%" PRIu32
            " adc_ok=%" PRIu32
            " adc_missed_sequence=%" PRIu32 " adc_sequence_gap=%" PRIu32
            " adc_missed_other=%" PRIu32 " drdy_event_overflow=%" PRIu32
            " adc_ring_overflow=%" PRIu32 " crc_bad=%" PRIu32
            " spi_errors=%" PRIu32 " ads_status_faults=%" PRIu32
            " event_depth_max=%" PRIu32 " raw_depth_max=%" PRIu32
            " deadline_depth_max=%" PRIu32 " led_queue_depth_max=%" PRIu32
            " task_wdt_subscribe_errors=%" PRIu32 " task_wdt_kick_errors=%" PRIu32
            " async_latency_min_us=%" PRIu32 " async_latency_max_us=%" PRIu32
            " async_latency_avg_us=%" PRIu32
            " async_on_pending_at_submit=%" PRIu32 " async_off_pending_at_submit=%" PRIu32
            " sample_clock_late_commits=%" PRIu32 " led_completion_timeout=%" PRIu32
            " sched_error=%d adc_error=%d"
            " frames=unsupported",
            control_boot_stage(),
            HLOAD(adc_missed), HLOAD(adc_backlog_catchup), HLOAD(adc_missed_backlog),
            HLOAD(adc_ok), HLOAD(adc_missed_sequence),
            HLOAD(adc_sequence_gap), HLOAD(adc_missed_other),
            HLOAD(drdy_event_overflow), HLOAD(adc_ring_overflow), HLOAD(crc_bad),
            HLOAD(spi_errors), HLOAD(ads_status_faults),
            adc.drdy_event_ring_depth_max, adc.raw_ring_depth_max,
            sched.deadline_ring_depth_max, HLOAD(async_command_queue_depth_max),
            HLOAD(task_wdt_subscribe_errors), HLOAD(task_wdt_kick_errors),
            HLOAD(async_latency_min_us), HLOAD(async_latency_max_us),
            HLOAD(async_latency_avg_us), HLOAD(async_on_pending_at_submit),
            HLOAD(async_off_pending_at_submit), HLOAD(sample_clock_late_commits),
            HLOAD(led_completion_timeout), sched.last_error, adc.last_error);
        if (wrote > 0 && (size_t)wrote < cap) {
            wrote += (int)nocti_v4_scheduler_append_led_timing(
                &s_scheduler, (char *)out + wrote, cap - (size_t)wrote);
        }
    } else {
        wrote = snprintf((char *)out, cap,
            "v4timing=1 boot82_bound=0 adc_missed=%" PRIu32
            " adc_backlog_catchup=%" PRIu32 " adc_missed_backlog=%" PRIu32
            " adc_sequence_gap=%" PRIu32
            " spi_min_us=%" PRIu32 " spi_max_us=%" PRIu32
            " spi_avg_us=%" PRIu32 " spi_lt40=%" PRIu32 " spi_40_59=%" PRIu32
            " spi_60_79=%" PRIu32 " spi_80_99=%" PRIu32 " spi_ge100=%" PRIu32
            " service_min_us=%" PRIu32 " service_max_us=%" PRIu32
            " service_avg_us=%" PRIu32 " service_lt80=%" PRIu32
            " service_80_124=%" PRIu32 " service_125_249=%" PRIu32
            " service_ge250=%" PRIu32 " service_unknown=%" PRIu32
            " service_over_budget=%" PRIu32
            " lag_max_us=%" PRIu32 " lag_lt40=%" PRIu32 " lag_40_79=%" PRIu32
            " lag_80_124=%" PRIu32 " lag_125_249=%" PRIu32 " lag_ge250=%" PRIu32
            " sched_deferred_max_us=%" PRIu32
            " event_depth_max=%" PRIu32 " ads_hwm_words=%" PRIu32
            " led_hwm_words=%" PRIu32 " sched_hwm_words=%" PRIu32
            " optical_hwm_words=%" PRIu32 " frame_hwm_words=%" PRIu32
            " dark_hwm_words=%" PRIu32 " ctrl_hwm_words=%" PRIu32
            " net_hwm_words=%" PRIu32 " cpu=not_measured",
            HLOAD(adc_missed), HLOAD(adc_backlog_catchup), HLOAD(adc_missed_backlog),
            HLOAD(adc_sequence_gap),
            HLOAD(spi_duration_min_us), HLOAD(spi_duration_max_us),
            HLOAD(spi_duration_avg_us), HLOAD(spi_service_lt_40_us),
            HLOAD(spi_service_40_to_59_us), HLOAD(spi_service_60_to_79_us),
            HLOAD(spi_service_80_to_99_us), HLOAD(spi_service_ge_100_us),
            HLOAD(adc_service_min_us), HLOAD(adc_service_max_us),
            HLOAD(adc_service_avg_us), HLOAD(adc_service_lt_80_us),
            HLOAD(adc_service_80_to_124_us), HLOAD(adc_service_125_to_249_us),
            HLOAD(adc_service_ge_250_us), HLOAD(adc_service_time_unknown),
            HLOAD(adc_service_over_budget),
            HLOAD(adc_queue_lag_max_us), HLOAD(adc_lag_lt_40_us),
            HLOAD(adc_lag_40_to_79_us), HLOAD(adc_lag_80_to_124_us),
            HLOAD(adc_lag_125_to_249_us), HLOAD(adc_lag_ge_250_us),
            HLOAD(scheduler_deferred_work_max_us),
            adc.drdy_event_ring_depth_max, HLOAD(ads_task_stack_watermark_bytes),
            HLOAD(async_led_task_stack_watermark_bytes),
            HLOAD(scheduler_task_stack_watermark_bytes),
            HLOAD(optical_task_stack_watermark_bytes),
            HLOAD(frame_consumer_task_stack_watermark_bytes),
            HLOAD(dark_consumer_task_stack_watermark_bytes), ctrl_hwm, net_hwm);
        if (wrote > 0 && (size_t)wrote < cap) {
            wrote += (int)nocti_v4_scheduler_append_sync(
                &s_scheduler, (char *)out + wrote, cap - (size_t)wrote);
        }
    }
    if (wrote < 0 || (size_t)wrote >= cap) return 0u;
    return (size_t)wrote;
}
#undef HLOAD
static esp_err_t control_init(void)
{
    esp_err_t result=nn2_config_init(); uint64_t boot=0u;
    if (result==ESP_OK) result=nn2_config_next_boot_id(&boot);
    if (result!=ESP_OK) return result;
    nn2_port_safety_t safety={.hold_off=control_off};
    if (nn2_app_comm_init(&s_nn2_app,boot,&safety,esp_random())!=NN2_OK) return ESP_FAIL;
    nn2_port_optical_t optical={.start=control_start,.state=control_state,.clear_fault=control_clear,
        .current_limit_ma=30u,.real_samples=true,
        .read_diag=control_read_diag,
        .policy_info="NoctiNIRS2/0.1.0;wire=ADR0004/1;baseline=V4_IDF61_NN2_CTRL;led=REAL_BRINGUP;short_policy=STRICT;adc=ADS131M04;frames=unsupported;fs_min=2;fs_max=10;diag=health,timing;console=removed"};
    if (nn2_app_comm_bind_optical(&s_nn2_app,&optical)!=NN2_OK) return ESP_FAIL;
    if (xTaskCreatePinnedToCore(control_worker,"v4_control",NOCTI_V4_RTOS_STACK_CONTROL_BYTES,NULL,NOCTI_V4_RTOS_PRIO_CONTROL,&s_control_worker,NOCTI_V4_RTOS_CORE_ACQ)!=pdPASS)
        return ESP_ERR_NO_MEM;
    result=nn2_wifi_init();
    static nn2_port_clock_t clock={.now_us=control_now};
    if (result==ESP_OK) result=nn2_net_init(&s_nn2_app,&clock,3333u);
    if (result==ESP_OK) result=nn2_wifi_start();
    if (result!=ESP_OK) { (void)control_off(NULL); return result; }
    if (!s_control_fault) control_set_boot_stage(nocti_v4_boot_stage_name(NOCTI_V4_BOOT_READY));
    if (xTaskCreatePinnedToCore(control_network,"v4_nn2_net",NOCTI_V4_RTOS_STACK_NET_BYTES,NULL,NOCTI_V4_RTOS_PRIO_NET,&s_control_network,NOCTI_V4_RTOS_CORE_SOFT)!=pdPASS) {
        (void)control_off(NULL); (void)nn2_wifi_stop(); return ESP_ERR_NO_MEM;
    }
    printf("V4_NN2_CONTROL boot=%" PRIu64 " ip=192.168.4.1 port=3333; V4 acquisition; frames=unsupported\n",boot);
    return ESP_OK;
}
#else
static bool control_start_allowed(void) { return true; }
static void control_allow_safe_restore(void) { }
static void control_set_boot_stage(const char *name) { (void)name; }
static void control_note_boot_fault(void) { }
#endif
