#ifndef NN2_RTOS_CONFIG_H
#define NN2_RTOS_CONFIG_H
/* Provisional diagnostic choices; no accepted timing/stack margin claim. */
#define NN2_RTOS_ADS131_CORE 1
#define NN2_RTOS_ADS131_PRIORITY 5
#define NN2_RTOS_ADS131_STACK_BYTES 4096u
#define NN2_RTOS_ADS131_SPI_WAIT_MS 20u
/* LED-only bring-up: no ADC/scheduler tasks coexist with this combined owner.
 * Provisional values for review; not accepted WCET/stack/affinity evidence. */
#define NN2_RTOS_LED_CORE 1
#define NN2_RTOS_LED_PRIORITY 22
#define NN2_RTOS_LED_STACK_BYTES 6144u
#define NN2_RTOS_LED_I2C_TIMEOUT_MS 20
#endif
