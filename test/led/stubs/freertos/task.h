#include "freertos/FreeRTOS.h"
void xTaskNotifyGive(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *wake);
uint32_t ulTaskNotifyTake(BaseType_t clear, uint32_t wait);
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name,
    uint32_t stack, void *ctx, unsigned priority, TaskHandle_t *task, int core);

TickType_t xTaskGetTickCount(void);
