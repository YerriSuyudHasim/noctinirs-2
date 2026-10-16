#include "nn2_adapter_esp32s3.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

static uint64_t clock_now_us(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time();
}

static void clock_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    esp_rom_delay_us(us);
}

nn2_port_clock_t nn2_adapter_clock_esp_port(void)
{
    nn2_port_clock_t p = { .now_us = clock_now_us, .delay_us = clock_delay_us, .ctx = NULL };
    return p;
}
