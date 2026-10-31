#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *higher_priority_woken);
