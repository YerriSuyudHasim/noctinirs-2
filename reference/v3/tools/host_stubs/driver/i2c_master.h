#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef int i2c_port_t; typedef void *i2c_master_bus_handle_t; typedef void *i2c_master_dev_handle_t;
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 7
#define I2C_EVENT_DONE 0
#define I2C_EVENT_NACK 1
#define I2C_EVENT_TIMEOUT 2
typedef struct {int i2c_port;int sda_io_num;int scl_io_num;int clk_source;uint8_t glitch_ignore_cnt;int intr_priority;size_t trans_queue_depth;struct{unsigned enable_internal_pullup:1;}flags;} i2c_master_bus_config_t;
typedef struct {int dev_addr_length;uint16_t device_address;uint32_t scl_speed_hz;uint32_t scl_wait_us;struct{unsigned disable_ack_check:1;}flags;} i2c_device_config_t;
typedef struct {int event;} i2c_master_event_data_t;
typedef bool (*i2c_master_callback_t)(i2c_master_dev_handle_t,const i2c_master_event_data_t*,void*);
typedef struct {i2c_master_callback_t on_trans_done;} i2c_master_event_callbacks_t;
static inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*c,i2c_master_bus_handle_t*b){(void)c;if(b)*b=(void*)1;return ESP_OK;}
static inline esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t b){(void)b;return ESP_OK;}
static inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b,const i2c_device_config_t*c,i2c_master_dev_handle_t*d){(void)b;(void)c;if(d)*d=(void*)1;return ESP_OK;}
static inline esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t d){(void)d;return ESP_OK;}
static inline esp_err_t i2c_master_register_event_callbacks(i2c_master_dev_handle_t d,const i2c_master_event_callbacks_t*c,void*u){(void)d;(void)c;(void)u;return ESP_OK;}
static inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t*b,size_t n,int t){(void)d;(void)b;(void)n;(void)t;return ESP_OK;}
static inline esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d,const uint8_t*tx,size_t tn,uint8_t*rx,size_t rn,int t){(void)d;(void)tx;(void)tn;(void)rx;(void)rn;(void)t;return ESP_OK;}
static inline esp_err_t i2c_master_bus_wait_all_done(i2c_master_bus_handle_t b,int t){(void)b;(void)t;return ESP_OK;}
static inline esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t b){(void)b;return ESP_OK;}
