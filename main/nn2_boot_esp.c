#include "nn2_boot_esp.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <stdint.h>

_Static_assert((int)ESP_RST_UNKNOWN == (int)NN2_RESET_UNKNOWN, "reset reason drift");
_Static_assert((int)ESP_RST_POWERON == (int)NN2_RESET_POWERON, "reset reason drift");
_Static_assert((int)ESP_RST_EXT == (int)NN2_RESET_EXT, "reset reason drift");
_Static_assert((int)ESP_RST_SW == (int)NN2_RESET_SW, "reset reason drift");
_Static_assert((int)ESP_RST_PANIC == (int)NN2_RESET_PANIC, "reset reason drift");
_Static_assert((int)ESP_RST_INT_WDT == (int)NN2_RESET_INT_WDT, "reset reason drift");
_Static_assert((int)ESP_RST_TASK_WDT == (int)NN2_RESET_TASK_WDT, "reset reason drift");
_Static_assert((int)ESP_RST_WDT == (int)NN2_RESET_WDT, "reset reason drift");
_Static_assert((int)ESP_RST_DEEPSLEEP == (int)NN2_RESET_DEEPSLEEP, "reset reason drift");
_Static_assert((int)ESP_RST_BROWNOUT == (int)NN2_RESET_BROWNOUT, "reset reason drift");
_Static_assert((int)ESP_RST_SDIO == (int)NN2_RESET_SDIO, "reset reason drift");
_Static_assert((int)ESP_RST_USB == (int)NN2_RESET_USB, "reset reason drift");
_Static_assert((int)ESP_RST_JTAG == (int)NN2_RESET_JTAG, "reset reason drift");
_Static_assert((int)ESP_RST_EFUSE == (int)NN2_RESET_EFUSE, "reset reason drift");
_Static_assert((int)ESP_RST_PWR_GLITCH == (int)NN2_RESET_PWR_GLITCH, "reset reason drift");
_Static_assert((int)ESP_RST_CPU_LOCKUP == (int)NN2_RESET_CPU_LOCKUP, "reset reason drift");

nn2_reset_reason_t nn2_adapter_read_reset_reason(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    if ((int)reason < 0 || (int)reason >= (int)NN2_RESET_COUNT_) {
        return NN2_RESET_UNKNOWN;
    }
    return (nn2_reset_reason_t)reason;
}

uint32_t nn2_adapter_spiram_bytes(void)
{
    size_t bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (bytes > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)bytes;
}
