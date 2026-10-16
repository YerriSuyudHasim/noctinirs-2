# NoctiNIRS Single-GPTimer Timebase Revision

## Purpose

This revision makes `nocti_timebase` the single monotonic timestamp domain for:

- optical scheduler tick deadlines
- ADS131M04 DRDY ISR timestamps
- PCA9956B asynchronous I2C completion timestamps
- UDP stream header timestamps
- processing-window comparisons

## Core design changes

1. One free-running GPTimer replaces the previous base-timer + tick-timer pair.
2. Clock source is explicitly `GPTIMER_CLK_SRC_XTAL` at 1 MHz.
3. Scheduler ticks use absolute one-shot alarms on the same free-running counter.
4. The callback receives the ideal scheduled alarm timestamp, not ISR service time.
5. A 50 us rearm guard prevents programming an alarm that becomes stale during ISR reconfiguration.
6. Delayed periods are represented by one callback plus `elapsed_ticks`; no long catch-up callback loop runs inside the ISR.
7. STOP/START uses a generation token so a stale interrupt cannot rearm a newer schedule.
8. GPTimer control/read functions and ISR paths are enabled for IRAM/cache-safe operation.
9. Timestamp read/rearm/catch-up diagnostics are exposed in STATUS and health logs.
10. I2C transactions that complete without a valid Nocti timebase timestamp are rejected.
11. LED completion timestamps in the past or later than 2000 us from command start are rejected as invalid.

## New health fields

- `tb_irq`: hardware GPTimer alarm interrupt count
- `tb_ticks`: total ideal periods represented
- `tb_catchup`: skipped/caught-up ideal periods
- `tb_late_max_us`: maximum alarm ISR dispatch lateness
- `tb_read_err`: failed GPTimer timestamp reads
- `tb_rearm_err`: failed dynamic alarm rearms

Healthy acquisition should normally show:

- `tb_irq == tb_ticks`
- `tb_catchup=0`
- `tb_read_err=0`
- `tb_rearm_err=0`
- `sched_missed=0`
- `process_wait_timeout=0`

## Required SDK configuration

The project `sdkconfig` and new `sdkconfig.defaults` enable:

- `CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM=y`
- `CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM=y`
- `CONFIG_GPTIMER_ISR_CACHE_SAFE=y`
- `CONFIG_GPTIMER_OBJ_CACHE_SAFE=y`

## Validation sequence

1. Build and flash without changing the current 400 kHz I2C setting.
2. Run SCAN for at least 2 minutes and inspect STATUS.
3. Confirm all `tb_*` error/catch-up counters remain zero.
4. Confirm `led_on_max_us` and `led_off_max_us` are realistic and no longer around 44 ms.
5. Confirm `process_wait_timeout=0`, `min_n_on>=8`, and `min_n_off>=8`.
6. Repeat with continuous Wi-Fi traffic for 10–30 minutes.
7. Only after this passes, raise PCA9956B I2C to 1 MHz and repeat.

## Validation performed here

- Static syntax checking of the revised `nocti_timebase.c` and `nocti_scheduler.c` using strict Clang warnings.
- Format-string argument-count checking for STATUS and periodic health logs.
- Repository scan confirming no remaining `esp_timer_get_time()` timestamp source.
- Configuration check confirming all required GPTimer IRAM/cache-safe options are enabled.

A full ESP-IDF cross-build and hardware timing test must still be performed on the target development environment.
