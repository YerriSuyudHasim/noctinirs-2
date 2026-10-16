#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef enum {GPIO_MODE_DISABLE=0,GPIO_MODE_INPUT=1,GPIO_MODE_OUTPUT=2} gpio_mode_t;
typedef enum {GPIO_INTR_DISABLE=0,GPIO_INTR_NEGEDGE=1} gpio_int_type_t;
typedef enum {GPIO_PULLUP_DISABLE=0,GPIO_PULLUP_ENABLE=1} gpio_pullup_t;
typedef enum {GPIO_PULLDOWN_DISABLE=0,GPIO_PULLDOWN_ENABLE=1} gpio_pulldown_t;
typedef struct {uint64_t pin_bit_mask;gpio_mode_t mode;gpio_pullup_t pull_up_en;gpio_pulldown_t pull_down_en;gpio_int_type_t intr_type;} gpio_config_t;
static inline esp_err_t gpio_config(const gpio_config_t*c){(void)c;return ESP_OK;}
static inline esp_err_t gpio_set_level(gpio_num_t g,uint32_t l){(void)g;(void)l;return ESP_OK;}
static inline esp_err_t gpio_install_isr_service(int f){(void)f;return ESP_OK;}
static inline esp_err_t gpio_isr_handler_add(gpio_num_t g,void(*f)(void*),void*a){(void)g;(void)f;(void)a;return ESP_OK;}
static inline esp_err_t gpio_intr_enable(gpio_num_t g){(void)g;return ESP_OK;}
static inline esp_err_t gpio_intr_disable(gpio_num_t g){(void)g;return ESP_OK;}
