#ifndef NN2_RTOS_CONFIG_H
#define NN2_RTOS_CONFIG_H
/* Provisional diagnostic choices; no accepted timing/stack margin claim. */
#define NN2_RTOS_ADS131_CORE 1
/* Preserve V4's deadline-first ADC service relative to this project's LED22.
 * Literal V4 priority20 would still be below LED22 here. Bench WCET pending. */
#define NN2_RTOS_ADS131_PRIORITY 23
#define NN2_RTOS_ADS131_STACK_BYTES 4096u
#define NN2_RTOS_ADS131_SPI_WAIT_MS 20u
/* LED owner stays priority22/core1; optional pairing is priority4/core1 and
 * ADS owner priority23/core1. Coexistence and the10ms drain budget are unqualified.
 * Provisional values for review; not accepted WCET/stack/affinity evidence. */
#define NN2_RTOS_LED_CORE 1
#define NN2_RTOS_LED_PRIORITY 22
#define NN2_RTOS_LED_STACK_BYTES 6144u
#define NN2_RTOS_LED_I2C_TIMEOUT_MS 20
#endif
