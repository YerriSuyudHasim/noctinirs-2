#pragma once
#include "freertos/FreeRTOS.h"
typedef void *SemaphoreHandle_t; typedef struct { int dummy; } StaticSemaphore_t;
static inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t*s){return s;}
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t*s){return s;}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s,TickType_t t){(void)s;(void)t;return pdTRUE;}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s){(void)s;return pdTRUE;}
