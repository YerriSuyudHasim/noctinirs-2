# NoctiNIRS firmware revision v15

**Date:** 2026-07-27  
**Target:** ESP32-S3, ESP-IDF 5.5.1  
**Application:** `main/app_main_fnirs_test.c`  
**Optical protocol:** `paired_v2_4_8k` — unchanged

## 1. Purpose

Revision v15 addresses the acquisition failures that remained after the move to
ADS131M04 at 8 kSPS. The main objectives are:

1. keep time-critical peripherals, ISRs, and acquisition tasks on the same
   ESP32-S3 core;
2. preserve every DRDY edge timestamp instead of retaining only the newest one;
3. guarantee one-to-one pairing between a DRDY event and one complete ADC frame;
4. recover safely when the ADS131M04 two-sample FIFO epoch becomes ambiguous;
5. keep Wi-Fi and fNIRS averaging away from the deterministic acquisition core;
6. add diagnostics that can prove the architecture is working on the device.

## 2. Sources reviewed

The revision was based on the uploaded official Texas Instruments example:

- `hal.c`
- `hal.h`
- `ads131m0x.c`
- `ads131m0x.h`

It was cross-checked against the uploaded ADS131M04 datasheet, ESP32-S3
datasheet, ESP32-S3 Technical Reference Manual, PCA9956B datasheet, and the v14
NoctiNIRS source.

### 2.1 What was adopted from the TI example

The following design points were incorporated into the NoctiNIRS driver:

- **24-bit SPI words and six-word ADS131M04 frames.** A normal four-channel
  frame is one response word, four channel words, and one CRC word.
- **SPI mode 1.** This remains unchanged in NoctiNIRS.
- **Falling-edge DRDY interrupt.** The ADC frame is read by a task after the
  interrupt, not from inside the GPIO ISR.
- **Pulse-format DRDY.** The TI header explicitly selects fixed-width negative
  pulses. NoctiNIRS now enforces this setting and verifies it at boot.
- **CCITT CRC and CRC-IN disabled.** The existing strict CRC-OUT validation is
  retained; an invalid conversion is recorded and never retried by consuming
  the next conversion.
- **Complete RESET command frame.** A RESET command must occupy the complete
  six-word frame before it is latched.
- **Register write readback.** Every driver register write is followed by an
  RREG readback and exact-value verification.
- **Non-reset SYNC pulse.** SYNC is used to clear and realign the conversion FIFO
  without erasing the configured register map.

### 2.2 What was intentionally not copied

The TI files are example/reference code, not a drop-in ESP-IDF acquisition
engine. The following parts were not adopted directly:

- the single `flag_nDRDY_INTERRUPT` boolean, because it collapses multiple DRDY
  events into one state;
- busy-loop timeout logic tied to the TI evaluation MCU;
- byte-by-byte SPI transfers and manual CS GPIO control;
- a complete mirrored 64-register map when strict readback of the registers
  used by NoctiNIRS is sufficient;
- the example's optional/disabled CRC-OUT calculation path.

No TI source file is compiled into the production firmware. Its protocol and
safety behavior were used as a reference while retaining the existing
NoctiNIRS ESP-IDF HAL.

## 3. Root causes found in v14

### 3.1 DRDY events were collapsed

The old driver kept only:

```text
DRDY count
latest DRDY timestamp
current waiting task
```

If two or more conversions occurred before the ADC task ran, all older
timestamps were lost. The task could then associate multiple frames with the
same or newest timestamp.

### 3.2 DRDY task registration changed for every sample

The waiting task was repeatedly installed and removed around every ADC read.
At 8 kSPS this created avoidable race windows and cross-core wakeups.

### 3.3 The GPIO ISR service was allocated from the application core

The ADC task and optical scheduler were pinned to core 1, but the GPIO ISR
service was installed from `app_main` on core 0. DRDY wakeups therefore crossed
cores and competed with Wi-Fi activity.

### 3.4 GPTimer allocation was also performed from the application core

The optical timebase was created before the acquisition-core tasks. This could
place its alarm interrupt on core 0 even though the scheduler ran on core 1.

### 3.5 A software queue cannot extend the ADS131M04 physical FIFO

The ADS131M04 output path stores only two samples. A large software ring can
preserve ISR evidence, but it cannot make a third unread hardware conversion
safe. Once more than two DRDY events are pending, timestamp-to-data pairing is
ambiguous and must be reset rather than guessed.

### 3.6 Pulse-format DRDY can expose a hidden gap

When a pulse-format DRDY conversion is not serviced, the ADC can suppress a
later pulse. Raw IRQ sequence numbers may still look consecutive even though a
conversion period is missing. Therefore both IRQ sequence and timestamp spacing
must be checked.

## 4. v15 core and task allocation

| Function | Core | Priority / context | Rationale |
|---|---:|---|---|
| `app_main`, command path | 0 | ESP-IDF main task | Non-deterministic control work |
| Wi-Fi driver/control/advertisement/TX | 0 | Existing Wi-Fi configuration | Keep RF/lwIP away from acquisition |
| fNIRS processing and frame assembly | 0 | Task priority 11 | Averaging and stream preparation are not edge-critical |
| GPTimer creation and alarm ISR | 1 | Platform-init task / ISR | Same core as optical scheduler |
| GPIO ISR service and ADS131 DRDY ISR | 1 | IRAM ISR | Same core as ADC consumer |
| ADS131 continuous consumer | 1 | Task priority 18 | Highest application task in the acquisition path |
| Optical scheduler | 1 | Task priority 17 | ADC can preempt scheduler while SPI frame is drained |
| PCA9956B asynchronous I2C allocation/callback | 1 | Temporary init/recovery task / ISR callback | LED edge completion timestamp stays on acquisition core |

The core IDs are measured at runtime and reported in `STATUS`; this table is a
configuration target, not a substitute for observed validation.

## 5. ADS131M04 event engine

### 5.1 ISR path

The DRDY ISR performs only bounded operations:

```text
read master GPTimer timestamp
increment IRQ sequence
push {sequence, timestamp} into static SPSC ring
notify permanent ADC consumer
exit ISR
```

It does not perform SPI, CRC, logging, averaging, queue allocation, or recovery.

### 5.2 Event ring

- static power-of-two ring: **128 events**;
- single producer: GPIO ISR;
- single consumer: ADS131 task;
- no heap allocation;
- notification is only a wake-up hint;
- ring contents are the source of truth.

At 8 kSPS the ring represents 16 ms of ISR evidence, but valid hardware backlog
is still limited to two samples.

### 5.3 One event, one SPI frame

For every accepted event the ADC task performs one contiguous 18-byte SPI
transaction under one CS assertion:

```text
NULL/response + CH0 + CH1 + CH2 + CH3 + CRC
```

The event timestamp is attached only after the SPI transaction succeeds.

### 5.4 Driver-level continuity guard

The main application configures:

```text
expected DRDY period = 125 us
timestamp tolerance  = ±40 us
```

Before SPI is started, the driver verifies:

- IRQ sequence increment is exactly one;
- timestamp is strictly monotonic;
- timestamp delta is inside one expected period;
- pending software depth does not exceed two.

This check occurs **before ambiguous data are shifted out**.

### 5.5 Bounded recovery

The driver disables DRDY capture, clears the software epoch, strobes a 5-us
non-reset SYNC pulse, and restarts from the next DRDY when:

- event backlog is greater than two;
- IRQ/timestamp continuity is not one conversion period;
- a SPI transaction fails.

The affected conversion is returned as an error and remains visible to the main
application as a sequence/timestamp gap. No old sample is substituted.

## 6. Register and protocol protections

v15 enforces and validates:

```text
MODE.WLENGTH      = 24 bit
MODE.RX_CRC_EN    = disabled
MODE.CRC_TYPE     = CCITT
MODE.DRDY_HiZ     = push-pull
MODE.DRDY_FMT     = fixed-width negative pulse
CLOCK             = 0x0F0A, OSR 512, HR, four channels, 8 kSPS
GAIN1             = 0x0000, all channels gain 1
CFG               = 0x0600, continuous conversion, GC_EN=0
```

The previous corrected six-bit RREG/WREG address encoding remains protected by
compile-time assertions.

Debug register reads/writes are rejected while continuous acquisition is
running so command frames cannot consume or reorder live conversion data.

## 7. Main application changes

### 7.1 Active boot validation

A temporary task pinned to core 1 performs a 1024-frame continuous active-read
test. It checks:

- measured sample rate within ±5% of 8 kSPS;
- minimum and maximum DRDY periods;
- CRC failures;
- IRQ sequence gaps and duplicates;
- timestamp gaps and non-monotonic timestamps;
- event-ring overflow and maximum depth;
- ISR-to-SPI and SPI transfer latency.

Acquisition is rejected if this test fails.

### 7.2 ADC history integrity

Every committed history record contains:

```text
DRDY timestamp
logical conversion sequence
raw IRQ sequence
CRC validity
timing validity
four signed ADC channels
```

The optical collector uses only one newest contiguous valid block. It stops at
CRC failure, timing failure, or sequence discontinuity.

### 7.3 Start and stop behavior

`START` now:

1. verifies/recover the LED driver while stopped;
2. resets runtime counters and processing generation;
3. forces all LEDs off;
4. binds the permanent ADC consumer;
5. performs SYNC and enables DRDY capture;
6. allows an LED-off prefill interval;
7. starts the absolute optical scheduler.

`STOP` now:

1. gates and stops the scheduler;
2. waits for an in-flight LED transaction to finish;
3. forces all LEDs off;
4. disables DRDY capture and directly wakes the blocked ADC task;
5. drains or invalidates outstanding processing jobs.

The old 5-ms DRDY timeout is no longer used as the normal stop mechanism.

## 8. LED and I2C changes

The PCA9956B electrical configuration remains unchanged:

```text
I2C speed       400 kHz
external pullups
synchronous register configuration/readback
asynchronous runtime fast path
single-register LEDOUT transition
```

Initialization and recovery now execute from a temporary core-1 task so the
asynchronous I2C controller and callback are allocated on the acquisition core.
The I2C HAL records both allocation core and first callback core.

## 9. New diagnostics

`STATUS` and the periodic health warning now include:

```text
drdy_capture
drdy_irq
drdy_push
drdy_pop
drdy_overflow
drdy_wait_timeout
drdy_sync
drdy_depth_max
fifo_risk
drdy_timing_fault
spi_recovery
fifo_recovery
drdy_isr_core
adc_consumer_core
irq_to_spi_max_us
spi_xfer_max_us
frame_service_max_us
led_i2c_init_core
led_i2c_callback_core
tb_init_core
tb_isr_core
core_plan_ok
platform_core
process_core
sched_core
adc_irq_dup
adc_ts_nonmono
adc_period_gap
```

The control response buffer was increased from 2048 to 4096 bytes to hold the
expanded status line without truncation.

## 10. Configuration intentionally unchanged

```text
ADS131M04 sample rate     8 kSPS
ADS131 SPI clock          8 MHz
SPI mode                  1
SPI transfer              polling, DMA off
Optical output rate       10 Hz
Optical slots             20 × 5 ms
LED ON/OFF budgets        2500 / 2500 us
Settling guard            500 us
Target K                  10 / 10
Minimum-valid K           8 / 8
PCA9956B I2C              400 kHz
Live-view wire protocol   paired_v2_4_8k
```

SPI 16 MHz and DMA are deliberately deferred until v15 proves zero event/FIFO
faults at 8 MHz. Optimization must not hide an event-architecture error.

## 11. Files changed

```text
README.md
components/nocti_common/include/nocti_version.h
components/nocti_fnirs/include/nocti_fnirs.h
components/nocti_fnirs/nocti_fnirs.c
components/nocti_timebase/include/nocti_timebase.h
components/nocti_timebase/nocti_timebase.c
components/nocti_hal_i2c/include/nocti_hal_i2c.h
components/nocti_hal_i2c/nocti_hal_i2c.c
components/nocti_led_driver/include/nocti_led_driver.h
components/nocti_led_driver/nocti_led_driver.c
components/nocti_scheduler/include/nocti_scheduler.h
components/nocti_scheduler/nocti_scheduler.c
main/app_main_fnirs_test.c
NOCTINIRS_FIRMWARE_REVISION_v15.md
```

## 12. Source-level validation completed here

The generated source was checked for:

- balanced braces, parentheses, brackets, comments, strings, and character
  literals in every modified C/H file;
- duplicate function definitions;
- removal of the old `wait_task`, `last_drdy_timestamp_us`,
  `wait_drdy_low`, and `wait_drdy_high` paths;
- printf-format argument count consistency for all modified status and health
  logs;
- power-of-two event-ring compile-time assertion;
- existing 8-kSPS timing and K compile-time assertions;
- existing RREG/WREG command compile-time assertions.

A real ESP-IDF build was **not** executed in this environment because the
ESP-IDF 5.5.1 Xtensa toolchain is not installed here. The source therefore must
be built locally before flashing.

## 13. Local build procedure

```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

Do not reuse the old `build` directory from v14.

## 14. Hardware validation procedure

1. Boot and confirm the 1024-frame ADS131 validation passes.
2. Send `MODE DARK`, `START`, and observe 30 seconds.
3. Send `STOP`, `MODE SINGLE`, select four representative left/right slots,
   and validate LED mapping.
4. Send `STOP`, `MODE SCAN`, `START`, and observe at least 60 seconds.
5. Send `STATUS` repeatedly.

### Required healthy core IDs after START

```text
drdy_isr_core=1
adc_consumer_core=1
tb_init_core=1
tb_isr_core=1
led_i2c_init_core=1
led_i2c_callback_core=1
core_plan_ok=1
platform_core=1
process_core=0
sched_core=1
```

### Primary acceptance targets

```text
adc_sps_validated=7600..8400
adc_missed=0
adc_irq_dup=0
adc_ts_nonmono=0
adc_period_gap=0
crc_bad=0
drdy_overflow=0
drdy_wait_timeout=0
fifo_risk=0
drdy_timing_fault=0
spi_recovery=0
fifo_recovery=0
underk_on=0
underk_off=0
partial=0
led_err=0
process_drop=0
process_wait_timeout=0
tb_catchup=0
sched_missed=0
sched_abort=0
```

Transient nonzero recovery counters mean the driver protected data integrity,
but they are not considered a passing steady-state result. The root latency or
scheduling cause must still be investigated.

## 15. Expected interpretation of failures

- `drdy_overflow > 0`: ISR evidence ring was full; acquisition task was blocked
  for far too long.
- `drdy_wait_timeout > 0`: no DRDY event arrived within the configured 5-ms
  guard; check CLKIN, ADC mode, GPIO routing, and capture state.
- `fifo_risk > 0`: more than two unread hardware conversions were possible;
  the driver discarded the epoch and synchronized.
- `drdy_timing_fault > 0`: IRQ or timestamp spacing was not one 125-us period;
  likely pulse suppression, ISR loss, or timebase fault.
- `spi_recovery > 0`: a SPI transaction failed and conversion alignment was
  intentionally reset.
- `irq_to_spi_max_us` approaching 125 us: the ADC consumer is not receiving
  enough CPU time.
- `spi_xfer_max_us` much greater than the nominal 18-us wire time: SPI/HAL or
  scheduling interference requires investigation.
- correct ADC counters but nonzero `sched_abort`: investigate LED I2C latency
  and optical scheduler deadlines rather than ADS131.
