/* Value contract: docs/ISSUE35_DIAGNOSTIC_CONTRACT.md. Raw counts only. */
#ifndef NN2_CORE_ADC_RECORD_H
#define NN2_CORE_ADC_RECORD_H
#include "nn2_core_types.h"
enum { NN2_ADC_QUALITY_GAP = 1u, NN2_ADC_QUALITY_OVERFLOW = 2u };
typedef struct {
    uint64_t read_complete_us; /* Software time, never conversion/optical edge. */
    int32_t channel[4]; /* CH0..CH3 simultaneous signed int24. */
    uint32_t sequence, session, quality;
    uint16_t status, crc_received;
    nn2_sample_src_t source;
} nn2_core_adc_record_t;
typedef enum { NN2_ADC_STOPPED, NN2_ADC_READY, NN2_ADC_RUNNING, NN2_ADC_FAULT } nn2_core_adc_state_t;
typedef enum {
    NN2_ADC_FAULT_NONE, NN2_ADC_FAULT_RESET, NN2_ADC_FAULT_ID,
    NN2_ADC_FAULT_READBACK, NN2_ADC_FAULT_CRC, NN2_ADC_FAULT_SPI,
    NN2_ADC_FAULT_DRDY, NN2_ADC_FAULT_GAP, NN2_ADC_FAULT_WATCHDOG
} nn2_core_adc_fault_t;
typedef struct {
    uint32_t produced, consumed, lost, overflow, crc_bad, spi_errors;
    uint32_t drdy_timeouts, adc_missed, loss_unknown, occupancy, max_occupancy;
    uint32_t sequence, session;
    nn2_core_adc_state_t state;
    nn2_core_adc_fault_t fault;
} nn2_core_adc_observation_t;
#endif
