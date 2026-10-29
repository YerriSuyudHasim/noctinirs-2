#ifndef NN2_RTOS_CONFIG_H
#define NN2_RTOS_CONFIG_H
/* ADR-0003 ordering: LED above the ADS band. Literal values and margins
 * remain provisional until coexistence bench qualification. */
#define NN2_RTOS_ADS131_CORE 1
#define NN2_RTOS_ADS131_PRIORITY 20
/* Pairing consumes raw samples and assembles frames on Core 0. */
#define NN2_RTOS_ADS131_CONSUMER_CORE 0
#define NN2_RTOS_ADS131_CONSUMER_PRIORITY 12
#define NN2_RTOS_ADS131_STACK_BYTES 4096u
#define NN2_RTOS_ADS131_SPI_WAIT_MS 20u
#define NN2_RTOS_SCHED_CORE 1
#define NN2_RTOS_SCHED_PRIORITY 21
#define NN2_RTOS_SCHED_STACK_BYTES 6144u
#define NN2_RTOS_LED_CORE 1
#define NN2_RTOS_LED_PRIORITY 22
#define NN2_RTOS_LED_STACK_BYTES 6144u
#define NN2_RTOS_LED_I2C_TIMEOUT_MS 20
#if NN2_RTOS_LED_PRIORITY <= NN2_RTOS_SCHED_PRIORITY || NN2_RTOS_SCHED_PRIORITY <= NN2_RTOS_ADS131_PRIORITY
#error "ADR-0003 / V4 requires LED > scheduler > ADS priority ordering"
#endif
/* Disable intrusive lock/phase timing; retain safety and error diagnostics. */
#define NN2_RTOS_LED_TIMING_DIAGNOSTICS 0
#endif
