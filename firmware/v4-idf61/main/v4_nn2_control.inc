/* Communication-only binding. All acquisition/timing stays in V4 components.
 * App/UDP have one Core0 owner. A Core1 worker serializes V4 start/stop and
 * resource allocation. Neither a UDP callback nor USB performs blocking I2C. */
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
static bool s_control_wanted, s_control_working, s_control_stop, s_control_fault, s_control_usb_off;
static bool s_control_clean=true;
static esp_err_t s_control_error=ESP_OK;
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
static void control_usb_stop(void)
{
    (void)control_off(NULL);
    portENTER_CRITICAL(&s_control_lock); s_control_usb_off=true; portEXIT_CRITICAL(&s_control_lock);
}
static uint64_t control_now(void *ctx) { (void)ctx; return (uint64_t)esp_timer_get_time(); }
static void control_network(void *ctx)
{
    (void)ctx;
    for (;;) {
        portENTER_CRITICAL(&s_control_lock);
        bool usb_off=s_control_usb_off; s_control_usb_off=false;
        portEXIT_CRITICAL(&s_control_lock);
        if (usb_off) (void)nn2_app_comm_safe_off(&s_nn2_app);
        nn2_wifi_status_t wifi=nn2_wifi_get_status();
        if (nn2_net_poll(wifi.connected,wifi.link_epoch)!=NN2_OK) (void)control_off(NULL);
        vTaskDelay(pdMS_TO_TICKS(1u));
    }
}
static void control_print_status(void)
{
    nn2_wifi_status_t wifi=nn2_wifi_get_status();
    portENTER_CRITICAL(&s_control_lock);
    uint8_t state=s_control_state; esp_err_t error=s_control_error;
    bool clean=s_control_clean;
    portEXIT_CRITICAL(&s_control_lock);
    printf("CONTROL_STATUS baseline=V4_IDF61_NN2_CTRL state=%u error=%s stopped_ack=%u ip=%s link=%u frames=unsupported\n",
        (unsigned)state,esp_err_to_name(error),(unsigned)clean,wifi.ipv4,(unsigned)wifi.connected);
}
static esp_err_t control_init(void)
{
    esp_err_t result=nn2_config_init(); uint64_t boot=0u;
    if (result==ESP_OK) result=nn2_config_next_boot_id(&boot);
    if (result!=ESP_OK) return result;
    nn2_port_safety_t safety={.hold_off=control_off};
    if (nn2_app_comm_init(&s_nn2_app,boot,&safety,esp_random())!=NN2_OK) return ESP_FAIL;
    nn2_port_optical_t optical={.start=control_start,.state=control_state,.clear_fault=control_clear,
        .current_limit_ma=30u,.real_samples=true,
        .policy_info="NoctiNIRS2/0.1.0;wire=ADR0004/1;baseline=V4_IDF61_NN2_CTRL;led=REAL_BRINGUP;short_policy=STRICT;adc=ADS131M04;frames=unsupported;fs_min=2;fs_max=10"};
    if (nn2_app_comm_bind_optical(&s_nn2_app,&optical)!=NN2_OK) return ESP_FAIL;
    if (xTaskCreatePinnedToCore(control_worker,"v4_control",8192,NULL,1u,&s_control_worker,1)!=pdPASS)
        return ESP_ERR_NO_MEM;
    result=nn2_wifi_init();
    static nn2_port_clock_t clock={.now_us=control_now};
    if (result==ESP_OK) result=nn2_net_init(&s_nn2_app,&clock,3333u);
    if (result==ESP_OK) result=nn2_wifi_start();
    if (result!=ESP_OK) { (void)control_off(NULL); return result; }
    if (xTaskCreatePinnedToCore(control_network,"v4_nn2_net",16384,NULL,4u,&s_control_network,0)!=pdPASS) {
        (void)control_off(NULL); (void)nn2_wifi_stop(); return ESP_ERR_NO_MEM;
    }
    printf("V4_NN2_CONTROL boot=%" PRIu64 " ip=192.168.4.1 port=3333; V4 acquisition; frames=unsupported\n",boot);
    return ESP_OK;
}
#else
static bool control_start_allowed(void) { return true; }
static void control_allow_safe_restore(void) { }
#endif
