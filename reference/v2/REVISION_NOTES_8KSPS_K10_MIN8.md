# NoctiNIRS firmware revision: ADS131M04 8 kSPS, K target 10 / minimum 8

Date: 2026-07-21

## Files to overwrite

Copy these files into the existing `NOCTINIRSv2` project while preserving their paths:

- `main/app_main_fnirs_test.c`
- `components/nocti_scheduler/nocti_scheduler.c`
- `components/nocti_scheduler/include/nocti_scheduler.h`

No live-view file changes are required. The paired-v2 binary record layout is unchanged.

## Acquisition configuration

- ADS131M04 data rate: **8000 SPS**
- ADS131M04 `CLOCK`: **0x0F0A** (OSR 512, four channels enabled, HR mode, assuming 8.192 MHz CLKIN)
- ADS131 SPI clock: **8 MHz**, SPI mode 1, DMA still disabled
- Optical frame rate: **10 Hz**
- Optical slots: **20 × 5 ms**
- LED ON / LED OFF budgets: **2500 / 2500 us**
- Settling guard after actual LED I2C STOP: **500 us**
- Target averaging window: **1250 us**
- Minimum-valid averaging window: **1000 us**
- K target: **10 samples for ON and 10 for LED-off intensity**
- K minimum valid: **8 samples for ON and 8 for LED-off intensity**

Quality classification:

- `n_used >= 10`: target/full-quality slot
- `8 <= n_used < 10`: valid but counted as `subtarget`
- `n_used < 8`: under-K; the frame becomes partial

## Scheduler changes

1. Scheduler admission now protects the **minimum-valid K=8 window**, not the target K=10 window. This leaves a nominal 1000 us budget for scheduler dispatch and the LED-ON transaction.
2. A late-dispatch abort that occurs before `SLOT_BEGIN` no longer performs a redundant PCA9956B all-off I2C write. This prevents one rejected slot from cascading into the next slots.
3. Abort causes are separated into:
   - `sched_abort_pre`: dispatch arrived too late before LED ON
   - `sched_abort_begin`: LED-ON callback returned an error
   - `sched_abort_post`: LED-ON transaction completed too late
4. The scheduler no longer uses the generic 1.5 ms busy-spin tail. It uses coarse RTOS sleep/yield followed by a 150 us precision tail, while the higher-priority ADC task remains able to preempt it.
5. LED-OFF remains tied to the absolute `tick + 2500 us` deadline.

## New diagnostics

`STATUS` and health logs now include:

- `subtarget_on`, `subtarget_off`
- `sched_abort_pre`, `sched_abort_begin`, `sched_abort_post`
- `k_on`, `k_off`, `k_min_on`, `k_min_off`
- `target_window_us`, `min_window_us`
- validated ADC rate and period range

## Expected first validation

After flashing, run `MODE SCAN`, then `START`, and observe at least 30–60 seconds.

Primary acceptance targets:

- startup register validation reads `CLOCK=0x0F0A`
- `adc_sps_validated` is close to 8000 SPS
- validation period is near 125 us
- `partial=0`
- `underk_on=0`, `underk_off=0`
- `min_n_on >= 8`, `min_n_off >= 8`
- preferably `min_n_on=10`, `min_n_off=10`
- preferably `subtarget_on=0`, `subtarget_off=0`
- `sched_abort=0`
- `sched_abort_pre=0`, `sched_abort_begin=0`, `sched_abort_post=0`
- `adc_missed=0`, `crc_bad=0`, `led_err=0`
- `process_drop=0`, `process_wait_timeout=0`

If slots are valid but `subtarget_*` increases, the firmware is still operating above K minimum, but timing should be investigated before collecting scientific data. Any increasing `partial`, `underk_*`, or `sched_abort_*` means the session should not be used for OD/MBLL.

## Validation performed before packaging

- C syntax/type/format checking of `app_main_fnirs_test.c` with Clang and project headers: passed.
- C syntax/type checking of `nocti_scheduler.c`: passed.
- Static timing assertions and source consistency checks: passed.

A full ESP-IDF build and hardware run were not available in the packaging environment. Build the project with ESP-IDF 5.5.1 and validate the runtime logs on the NoctiNIRS board before data collection.
