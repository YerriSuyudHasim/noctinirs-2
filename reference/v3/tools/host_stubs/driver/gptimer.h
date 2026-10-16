#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *gptimer_handle_t;
#define GPTIMER_CLK_SRC_DEFAULT 0
#define GPTIMER_CLK_SRC_XTAL 1
#define GPTIMER_COUNT_UP 0
typedef struct {int clk_src;int direction;uint32_t resolution_hz;int intr_priority;struct{unsigned intr_shared:1;}flags;} gptimer_config_t;
typedef struct {uint64_t alarm_count;uint64_t reload_count;struct{unsigned auto_reload_on_alarm:1;}flags;} gptimer_alarm_config_t;
typedef struct {uint64_t count_value;uint64_t alarm_value;} gptimer_alarm_event_data_t;
typedef bool (*gptimer_alarm_cb_t)(gptimer_handle_t,const gptimer_alarm_event_data_t*,void*);
typedef struct {gptimer_alarm_cb_t on_alarm;} gptimer_event_callbacks_t;
static inline esp_err_t gptimer_new_timer(const gptimer_config_t*c,gptimer_handle_t*h){(void)c;if(h)*h=(void*)1;return ESP_OK;}
static inline esp_err_t gptimer_del_timer(gptimer_handle_t h){(void)h;return ESP_OK;}
static inline esp_err_t gptimer_enable(gptimer_handle_t h){(void)h;return ESP_OK;}
static inline esp_err_t gptimer_disable(gptimer_handle_t h){(void)h;return ESP_OK;}
static inline esp_err_t gptimer_start(gptimer_handle_t h){(void)h;return ESP_OK;}
static inline esp_err_t gptimer_stop(gptimer_handle_t h){(void)h;return ESP_OK;}
static inline esp_err_t gptimer_get_raw_count(gptimer_handle_t h,uint64_t*c){(void)h;if(c)*c=0;return ESP_OK;}
static inline esp_err_t gptimer_register_event_callbacks(gptimer_handle_t h,const gptimer_event_callbacks_t*c,void*u){(void)h;(void)c;(void)u;return ESP_OK;}
static inline esp_err_t gptimer_set_alarm_action(gptimer_handle_t h,const gptimer_alarm_config_t*c){(void)h;(void)c;return ESP_OK;}
