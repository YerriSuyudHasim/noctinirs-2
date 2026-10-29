#pragma once

#include <stdint.h>

#include "driver/spi_common.h"
#include "nocti_v4_rtos.h"

/* Central V4-3 ADS131M04 acquisition contract. Board GPIOs live in
 * nocti_v4_board.h; no ADS timing or task parameter is scattered elsewhere. */
#define NOCTI_V4_ADS_SPI_HOST                    SPI2_HOST
#define NOCTI_V4_ADS_SPI_CLOCK_HZ                8000000u
#define NOCTI_V4_ADS_SPI_MODE                    1u
#define NOCTI_V4_ADS_SPI_QUEUE_SIZE              1u
#define NOCTI_V4_ADS_SPI_CS_GUARD_CYCLES         1u
#define NOCTI_V4_ADS_CLKIN_HZ                    8192000u
#define NOCTI_V4_ADS_SAMPLE_RATE_HZ              8000u
#define NOCTI_V4_ADS_EXPECTED_PERIOD_US          125u
#define NOCTI_V4_ADS_PERIOD_GAP_THRESHOLD_US     188u
#define NOCTI_V4_ADS_WORD_BITS                   24u
#define NOCTI_V4_ADS_WORD_BYTES                  3u
#define NOCTI_V4_ADS_CHANNEL_COUNT               4u
#define NOCTI_V4_ADS_FRAME_WORDS                 6u
#define NOCTI_V4_ADS_FRAME_BYTES                 \
    (NOCTI_V4_ADS_WORD_BYTES * NOCTI_V4_ADS_FRAME_WORDS)
#define NOCTI_V4_ADS_OSR                         512u
#define NOCTI_V4_ADS_CLOCK_REGISTER_8KSPS        0x0F0Au
#define NOCTI_V4_ADS_GAIN_REGISTER_X1            0x0000u
#define NOCTI_V4_ADS_CFG_CONTINUOUS              0x0600u
#define NOCTI_V4_ADS_CHANNEL_CFG_DEFAULT         0x0000u

#define NOCTI_V4_DRDY_EVENT_RING_CAPACITY        128u
/* V4-6 hardware observed a maximum raw-ring depth of one over the final
 * ~206-second quiet run. 256 records retain 32 ms of lossless backlog at
 * 8 kSPS while recovering 43,008 bytes of internal RAM for the bounded Wi-Fi
 * subsystem. Overflow remains a hard, counted acquisition fault. */
#define NOCTI_V4_RAW_SAMPLE_RING_CAPACITY        256u
/* Startup qualification still rejects two events left behind the one being
 * serviced. RUNNING catch-up is nocti_v4_backlog.h: overwritten events are
 * counted, and only a 16-deep or 1 ms-stale queue faults the session. */
#define NOCTI_V4_ADS_MAX_EVENT_BACKLOG           2u
#define NOCTI_V4_ADS_RUNTIME_PERIOD_SNAPSHOT_COUNT 8u
#define NOCTI_V4_ADS_RATE_VALIDATION_SAMPLES     1024u
#define NOCTI_V4_ADS_SYNC_DISCARD_FRAMES         3u
#define NOCTI_V4_ADS_EXPECTED_RESYNC_BUDGET      1u
#define NOCTI_V4_ADS_RATE_TOLERANCE_PERCENT      5u
#define NOCTI_V4_ADS_EVENT_WAIT_TIMEOUT_MS       100u
#define NOCTI_V4_ADS_CONTROL_TIMEOUT_MS          3000u
#define NOCTI_V4_ADS_INIT_TIMEOUT_MS             8000u
#define NOCTI_V4_ADS_DRDY_STALL_TIMEOUT_US       5000u

#define NOCTI_V4_CORE_DARK_DIAGNOSTIC            NOCTI_V4_RTOS_CORE_SOFT
#define NOCTI_V4_CORE_REALTIME                   NOCTI_V4_RTOS_CORE_ACQ
#define NOCTI_V4_PRIO_ADC                        NOCTI_V4_RTOS_PRIO_ADS
#define NOCTI_V4_PRIO_DARK_DIAGNOSTIC            NOCTI_V4_RTOS_PRIO_DARK
#define NOCTI_V4_STACK_ADC_BYTES                 NOCTI_V4_RTOS_STACK_ADS_BYTES
#define NOCTI_V4_STACK_DARK_DIAGNOSTIC_BYTES     NOCTI_V4_RTOS_STACK_DARK_BYTES

_Static_assert((NOCTI_V4_DRDY_EVENT_RING_CAPACITY &
                (NOCTI_V4_DRDY_EVENT_RING_CAPACITY - 1u)) == 0u,
               "DRDY event ring must be a power of two");
_Static_assert((NOCTI_V4_RAW_SAMPLE_RING_CAPACITY &
                (NOCTI_V4_RAW_SAMPLE_RING_CAPACITY - 1u)) == 0u,
               "raw sample ring must be a power of two");
_Static_assert(NOCTI_V4_ADS_FRAME_BYTES == 18u,
               "24-bit status plus four channels plus output CRC is 18 bytes");
_Static_assert(NOCTI_V4_ADS_CLKIN_HZ / (2u * NOCTI_V4_ADS_OSR) ==
                   NOCTI_V4_ADS_SAMPLE_RATE_HZ,
               "8.192 MHz CLKIN and OSR 512 must produce 8 kSPS");
