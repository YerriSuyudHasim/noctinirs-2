#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
typedef enum { eNoAction=0, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite } eNotifyAction;
static inline BaseType_t xTaskCreatePinnedToCore(void(*f)(void*),const char*n,uint32_t st,void*a,UBaseType_t p,TaskHandle_t*h,BaseType_t c){(void)f;(void)n;(void)st;(void)a;(void)p;(void)c;if(h)*h=(void*)1;return pdPASS;}
static inline void vTaskDelete(TaskHandle_t t){(void)t;}
static inline void vTaskDelay(TickType_t t){(void)t;}
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void*)1;}
static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t){(void)t;return 1024;}
static inline BaseType_t xTaskNotify(TaskHandle_t t,uint32_t v,eNotifyAction a){(void)t;(void)v;(void)a;return pdTRUE;}
static inline BaseType_t xTaskNotifyFromISR(TaskHandle_t t,uint32_t v,eNotifyAction a,BaseType_t*w){(void)t;(void)v;(void)a;if(w)*w=pdFALSE;return pdTRUE;}
static inline void xTaskNotifyGive(TaskHandle_t t){(void)t;}
static inline void vTaskNotifyGiveFromISR(TaskHandle_t t,BaseType_t*w){(void)t;if(w)*w=pdFALSE;}
static inline uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t t){(void)clear;(void)t;return 0;}
static inline BaseType_t xTaskNotifyWait(uint32_t a,uint32_t b,uint32_t*c,TickType_t t){(void)a;(void)b;if(c)*c=0;(void)t;return pdFALSE;}
