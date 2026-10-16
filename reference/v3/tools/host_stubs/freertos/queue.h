#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t; typedef struct { int dummy; } StaticQueue_t;
static inline QueueHandle_t xQueueCreateStatic(UBaseType_t n,UBaseType_t sz,uint8_t*b,StaticQueue_t*s){(void)n;(void)sz;(void)b;return s;}
static inline BaseType_t xQueueSend(QueueHandle_t q,const void*i,TickType_t t){(void)q;(void)i;(void)t;return pdTRUE;}
static inline BaseType_t xQueueReceive(QueueHandle_t q,void*i,TickType_t t){(void)q;(void)i;(void)t;return pdFALSE;}
static inline BaseType_t xQueueReset(QueueHandle_t q){(void)q;return pdTRUE;}
