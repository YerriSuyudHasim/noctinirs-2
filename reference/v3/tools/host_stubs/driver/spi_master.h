#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
typedef void *spi_device_handle_t;
typedef struct {int mosi_io_num;int miso_io_num;int sclk_io_num;int quadwp_io_num;int quadhd_io_num;int data4_io_num;int data5_io_num;int data6_io_num;int data7_io_num;int max_transfer_sz;uint32_t flags;int intr_flags;} spi_bus_config_t;
#define SPI_CLK_SRC_DEFAULT 0
typedef struct {int command_bits;int address_bits;int dummy_bits;int mode;int duty_cycle_pos;int cs_ena_pretrans;int cs_ena_posttrans;int clock_speed_hz;int input_delay_ns;int clock_source;int spics_io_num;uint32_t flags;int queue_size;void*pre_cb;void*post_cb;} spi_device_interface_config_t;
typedef struct {uint32_t flags;size_t length;size_t rxlength;const void*tx_buffer;void*rx_buffer;} spi_transaction_t;
static inline esp_err_t spi_bus_initialize(spi_host_device_t h,const spi_bus_config_t*c,int d){(void)h;(void)c;(void)d;return ESP_OK;}
static inline esp_err_t spi_bus_free(spi_host_device_t h){(void)h;return ESP_OK;}
static inline esp_err_t spi_bus_add_device(spi_host_device_t h,const spi_device_interface_config_t*c,spi_device_handle_t*d){(void)h;(void)c;if(d)*d=(void*)1;return ESP_OK;}
static inline esp_err_t spi_bus_remove_device(spi_device_handle_t d){(void)d;return ESP_OK;}
static inline esp_err_t spi_device_acquire_bus(spi_device_handle_t d,TickType_t t){(void)d;(void)t;return ESP_OK;}
static inline void spi_device_release_bus(spi_device_handle_t d){(void)d;}
static inline esp_err_t spi_device_polling_transmit(spi_device_handle_t d,spi_transaction_t*t){(void)d;(void)t;return ESP_OK;}
