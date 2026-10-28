/* Real communication, optional LED-only bench drive; no optical acquisition. */
#include "sdkconfig.h"

#if CONFIG_NN2_PROFILE_COMMUNICATION
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nn2_adapter_esp32s3.h"
#include "nn2_app_comm.h"
#include "nn2_app_battery.h"
#include "nn2_board.h"
#include "nn2_board_power.h"
#include "nn2_config.h"
#include "nn2_net.h"
#include "nn2_provision.h"
#include "nn2_wifi.h"
#if CONFIG_NN2_LED_BRINGUP
#include "nn2_led_bringup.h"
#endif

static const char *TAG = "nn2_main";
static nn2_app_comm_storage_t s_application;
static nn2_app_battery_storage_t s_battery;

static nn2_status_t usb_safe_off(void)
{
    return nn2_app_comm_safe_off(&s_application);
}

static void describe_status(char *out, size_t capacity)
{
    const esp_app_desc_t *image = esp_app_get_description();
    char image_sha[65];
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        image_sha[2 * i] = hex[image->app_elf_sha256[i] >> 4];
        image_sha[2 * i + 1] = hex[image->app_elf_sha256[i] & 15];
    }
    image_sha[64] = 0;
    nn2_wifi_status_t wifi = nn2_wifi_get_status();
    nn2_net_status_t net = nn2_net_get_status();
    nn2_app_comm_status_t app = nn2_app_comm_get_status(&s_application);
    nn2_board_boot_info_t board = nn2_board_get_boot_info();
    nn2_provision_status_t usb = nn2_provision_get_status();
    nn2_app_battery_status_t battery = nn2_app_battery_get_status(&s_battery);
    nn2_port_clock_t clock = nn2_adapter_clock_esp_port();
    uint64_t battery_age_ms = battery.valid ?
        (clock.now_us(clock.ctx) - battery.last_success_us) / 1000u : 0;
    snprintf(out, capacity,
        "STATUS source=%s image=%s ip=%s link=%u state=%u fault=%u boot=%" PRIu64
        " reset=%u heap=%u min_heap=%u stack=%u rx=%" PRIu32 " tx=%" PRIu32 " invalid=%" PRIu32
        " socket_err=%" PRIu32 " send_err=%" PRIu32 " usb_reject=%" PRIu32 " usb_drop=%" PRIu32
        " off_requested=%u wifi_err=%d clients=%u ap=%u usb_timeout=%" PRIu32
        " battery_valid=%u battery_uv=%" PRIu32 " battery_mperm=%" PRIu32
        " battery_age_ms=%" PRIu64 " battery_error=%u",
        NN2_SOURCE_SHA, image_sha, wifi.ipv4, (unsigned)wifi.connected, (unsigned)app.state, (unsigned)app.fault, app.boot_id,
        board.reset_reason, (unsigned)esp_get_free_heap_size(),
        (unsigned)esp_get_minimum_free_heap_size(), (unsigned)uxTaskGetStackHighWaterMark(NULL),
        net.received, net.replied, net.invalid, net.socket_errors, net.send_errors,
        usb.rejected_lines, usb.tx_drops, (unsigned)board.reset_asserted_by_software,
        (int)wifi.last_error, (unsigned)wifi.clients, (unsigned)wifi.ap_active, usb.timed_out_lines,
        (unsigned)battery.valid, battery.sample.voltage_uv, battery.sample.soc_millipercent,
        battery_age_ms, (unsigned)battery.last_result);
#if CONFIG_NN2_LED_BRINGUP
    nn2_led_bringup_status_t led = nn2_led_bringup_status();
    size_t used = strlen(out);
    if (used < capacity && !nn2_led_bringup_format_status(out + used, capacity - used, &led))
        snprintf(out, capacity, "ERR status-capacity\n");
#else
    size_t used = strlen(out);
    if (used < capacity) snprintf(out + used, capacity - used, "\n");
#endif
}


void app_main(void)
{
    const char *stage = "board safe boot";
    nn2_port_clock_t clock = nn2_adapter_clock_esp_port();
    esp_err_t result = nn2_board_init_safe();
    if (result != ESP_OK) {
        goto fail;
    }
    stage = "board power";
    result = nn2_board_power_init(true);
    if (result != ESP_OK) goto fail;
    stage = "power I2C release";
    result = nn2_board_power_release_i2c();
    if (result != ESP_OK) goto fail;
    stage = "initial battery telemetry";
    nn2_port_battery_t battery_port = nn2_board_battery_port();
    if (nn2_app_battery_init(&s_battery, &battery_port) != NN2_OK ||
        nn2_app_battery_poll(&s_battery, clock.now_us(clock.ctx), false, true) != NN2_OK) {
        result = ESP_FAIL;
        goto fail;
    }
    ESP_LOGI(TAG, "battery monitor: idle=60s acquisition=300s; charge enable requested");
#if CONFIG_NN2_LED_BRINGUP
    stage = "LED boot off/readback and owner";
    result = nn2_led_bringup_init();
    if (result != ESP_OK) goto fail;
#endif
    stage = "NVS";
    result = nn2_config_init();
    if (result != ESP_OK) {
        goto fail;
    }
    stage = "USB diagnostics";
    result = nn2_provision_init(NULL, describe_status, usb_safe_off);
    if (result != ESP_OK) {
        goto fail;
    }
    stage = "boot identity";
    uint64_t boot_id;
    result = nn2_config_next_boot_id(&boot_id);
    if (result != ESP_OK) {
        goto fail;
    }
    stage = "SoftAP resources";
    result = nn2_wifi_init();
    if (result != ESP_OK) {
        goto fail;
    }
    stage = "command application";
    nn2_port_safety_t safety = nn2_board_safety_port();
#if CONFIG_NN2_LED_BRINGUP
    safety = nn2_led_bringup_safety_port();
#endif
    if (nn2_app_comm_init(&s_application, boot_id, &safety, esp_random()) != NN2_OK) {
        result = ESP_FAIL;
        goto fail;
    }
#if CONFIG_NN2_LED_BRINGUP
    nn2_port_optical_t optical = nn2_led_bringup_port();
    if (nn2_app_comm_bind_optical(&s_application, &optical) != NN2_OK) {
        stage = "LED-only command binding";
        result = ESP_FAIL;
        goto fail;
    }
    ESP_LOGW(TAG, "REAL LED BRING-UP: explicit START can energize emitters; Fs default 5Hz, 30mA, no ADC/frames");
#endif
    stage = "UDP service";
    result = nn2_net_init(&s_application, &clock, CONFIG_NN2_UDP_PORT);
    if (result != ESP_OK) {
        goto fail;
    }
    ESP_LOGI(TAG, "v0.1.0 ADR0004/1 boot=%" PRIu64 " SIMULATED; RESET_LED held low", boot_id);
    ESP_LOGI(TAG, "SoftAP NoctiNIRS; device 192.168.4.1; UDP port %u", (unsigned)CONFIG_NN2_UDP_PORT);
    stage = "network startup";
    result = nn2_wifi_start();
    if (result != ESP_OK) goto fail;

    for (;;) {
        (void)nn2_provision_poll(clock.now_us(clock.ctx));
        nn2_wifi_status_t wifi = nn2_wifi_get_status();
        if (nn2_net_poll(wifi.connected, wifi.link_epoch) != NN2_OK) {
            stage = "communication safety";
            result = ESP_FAIL;
            goto fail;
        }
#if !CONFIG_NN2_LED_BRINGUP
        result = nn2_board_hold_emitters_off();
        if (result != ESP_OK) {
            stage = "emitter reset maintenance";
            goto fail;
        }
#endif
        nn2_app_comm_status_t app = nn2_app_comm_get_status(&s_application);
        bool battery_window = true;
#if CONFIG_NN2_LED_BRINGUP
        nn2_led_bringup_status_t led = nn2_led_bringup_status();
        battery_window = led.bus_released && led.state == NN2_OPTICAL_IDLE;
#endif
        nn2_status_t battery_result = nn2_app_battery_poll(&s_battery,
            clock.now_us(clock.ctx), app.state == NN2_COMM_STREAMING ||
                app.state == NN2_COMM_STARTING || app.state == NN2_COMM_STOPPING,
            battery_window);
        if (battery_result != NN2_OK) {
            ESP_LOGW(TAG, "battery telemetry failed: %s; cached sample retained",
                nn2_core_status_str(battery_result));
        }
        vTaskDelay(pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
    }

fail:;
#if CONFIG_NN2_LED_BRINGUP
    nn2_port_safety_t led_safety = nn2_led_bringup_safety_port();
    (void)led_safety.hold_off(led_safety.ctx);
#endif
    esp_err_t charge_off = nn2_board_power_set_charge_enabled(false);
    esp_err_t power_release = nn2_board_power_release_i2c();
    ESP_LOGE(TAG, "power stop: charge-disable=%s I2C-release=%s",
        esp_err_to_name(charge_off), esp_err_to_name(power_release));
    (void)nn2_net_stop();
    (void)nn2_wifi_stop();
    esp_err_t off = nn2_board_hold_emitters_off();
    ESP_LOGE(TAG, "startup/runtime stopped at %s: %s; off-request=%s; reboot required", stage,
        esp_err_to_name(result), esp_err_to_name(off));
}
#else
void nn2_legacy_profile_run(void);
void app_main(void)
{
    nn2_legacy_profile_run();
}
#endif
