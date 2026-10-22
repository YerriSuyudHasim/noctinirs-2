#include "nn2_wifi_bringup.h"

#include "nn2_ad_cred.h"
#include "nn2_ad_prov.h"
#include "nn2_ad_wifi.h"
#include "nn2_app_wifi.h"
#include "nn2_rtos_config.h"
#include "nn2_wifi_limits.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "nn2_bringup";

static nn2_port_wifi_cred_store_t s_store;
static nn2_port_prov_console_t s_console;
static nn2_port_wifi_station_t s_station;
static nn2_wifi_credentials_t s_handoff;
static TaskHandle_t s_net_task;
static StaticTask_t s_net_tcb;
static StackType_t s_net_stack[NN2_RTOS_STACK_NET_BYTES];
static StaticTask_t s_prov_tcb;
static StackType_t s_prov_stack[NN2_RTOS_STACK_PROV_BYTES];

static void net_task(void *arg)
{
    nn2_wifi_credentials_t creds;
    nn2_status_t status;

    (void)arg;
    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NN2_WIFI_EVENT_WAIT_MS)) > 0u) {
            break;
        }
    }
    creds = s_handoff;
    nn2_wifi_cred_wipe(&s_handoff);
    status = nn2_app_wifi_connect_from_creds(&s_station, &creds);
    nn2_wifi_cred_wipe(&creds);
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "sta connect request failed: %d", (int)status);
    }
    for (;;) {
        nn2_wifi_sta_evt_t evt;

        status = s_station.wait_event(s_station.ctx, &evt, NN2_WIFI_EVENT_WAIT_MS);
        if (status == NN2_ERR_TIMEOUT) {
            nn2_wifi_sta_status_t link;

            memset(&link, 0, sizeof(link));
            if (s_station.get_status != NULL &&
                s_station.get_status(s_station.ctx, &link) == NN2_OK) {
                status = nn2_app_wifi_on_wait_timeout(&s_station, &link,
                                                           nn2_ad_wifi_connect_guard_ms());
                if (status != NN2_OK) {
                    ESP_LOGW(TAG, "sta liveness schedule failed: %d", (int)status);
                }
            }
            continue;
        }
        if (status != NN2_OK) {
            ESP_LOGE(TAG, "sta wait failed: %d", (int)status);
            continue;
        }
        if (evt == NN2_WIFI_STA_EVT_LINK_DOWN) {
            nn2_wifi_sta_status_t link;

            memset(&link, 0, sizeof(link));
            status = nn2_app_wifi_on_link_event(&s_station, NN2_WIFI_LINK_DOWN, &link);
            ESP_LOGI(TAG, "sta link down (%d)", (int)status);
        } else if (evt == NN2_WIFI_STA_EVT_LINK_UP) {
            nn2_wifi_sta_status_t link;

            memset(&link, 0, sizeof(link));
            (void)nn2_app_wifi_on_link_event(&s_station, NN2_WIFI_LINK_UP, &link);
            ESP_LOGI(TAG, "sta link up");
        } else if (evt == NN2_WIFI_STA_EVT_RECONNECT_DUE) {
            /* A RECONNECT_DUE still queued after LINK_UP is safe.
               reconnect_now sees the link up and returns NN2_OK, so the
               reschedule below does not run. An in-flight attempt is
               left alone the same way. */
            status = NN2_ERR_INVALID_ARG;
            if (s_station.reconnect_now != NULL) {
                status = s_station.reconnect_now(s_station.ctx);
            }
            if (status != NN2_OK) {
                nn2_status_t scheduled;

                ESP_LOGW(TAG, "sta reconnect request failed: %d", (int)status);
                scheduled = nn2_app_wifi_on_reconnect_result(&s_station, status);
                if (scheduled != NN2_OK) {
                    ESP_LOGW(TAG, "sta reconnect reschedule failed: %d", (int)scheduled);
                }
            }
        }
    }
}

static void prov_task(void *arg)
{
    nn2_app_wifi_provision_deps_t deps;

    (void)arg;
    deps.store = &s_store;
    deps.console = &s_console;
    deps.console_timeout_ms = NN2_WIFI_PROV_TIMEOUT_MS;
    for (;;) {
        nn2_wifi_credentials_t creds;
        nn2_status_t status;

        nn2_wifi_cred_wipe(&creds);
        status = nn2_app_wifi_provision_if_needed(&deps, &creds);
        if (status == NN2_OK) {
            s_handoff = creds;
            nn2_wifi_cred_wipe(&creds);
            xTaskNotifyGive(s_net_task);
            vTaskDelete(NULL);
        }
        nn2_wifi_cred_wipe(&creds);
        ESP_LOGW(TAG, "provision attempt failed: %d", (int)status);
        vTaskDelay(pdMS_TO_TICKS(NN2_WIFI_PROV_RETRY_MS));
    }
}

nn2_status_t nn2_wifi_bringup(void)
{
    nn2_status_t status;
    TaskHandle_t prov_task_handle;

    nn2_wifi_cred_wipe(&s_handoff);
    status = nn2_ad_cred_init(&s_store);
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "credential store init failed: %d", (int)status);
        return status;
    }
    status = nn2_ad_wifi_init(&s_station);
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "sta init failed: %d", (int)status);
        return status;
    }
    status = nn2_ad_prov_init(&s_console);
    if (status != NN2_OK) {
        ESP_LOGE(TAG, "console init failed: %d", (int)status);
        return status;
    }
    s_net_task = xTaskCreateStaticPinnedToCore(net_task,
                                               NN2_TASK_NAME_NET,
                                               NN2_RTOS_STACK_NET_BYTES,
                                               NULL,
                                               NN2_RTOS_PRIO_NET,
                                               s_net_stack,
                                               &s_net_tcb,
                                               NN2_RTOS_CORE_NET);
    if (s_net_task == NULL) {
        return NN2_ERR_NO_MEM;
    }
    prov_task_handle = xTaskCreateStaticPinnedToCore(prov_task,
                                                     NN2_TASK_NAME_PROV,
                                                     NN2_RTOS_STACK_PROV_BYTES,
                                                     NULL,
                                                     NN2_RTOS_PRIO_PROV,
                                                     s_prov_stack,
                                                     &s_prov_tcb,
                                                     NN2_RTOS_CORE_PROV);
    if (prov_task_handle == NULL) {
        return NN2_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "tasks %s and %s pinned to core 0", NN2_TASK_NAME_NET, NN2_TASK_NAME_PROV);
    return NN2_OK;
}
