# NoctiNIRS Firmware Revision v16

**Firmware version:** `0.16.0`
**Base:** NoctiNIRS v15 + local two-line `nocti_timebase_get_status()` fix
**Primary objective:** remove serial-console activity from the active acquisition interval so UART/USB logging cannot disturb ADS131M04 DRDY servicing or optical scheduler deadlines.

## 1. Background

V15 successfully corrected the GPTimer epoch, DRDY event ring, ISR/core placement, FIFO recovery, and PCA9956B asynchronous edge timestamps. The remaining log showed occasional:

- `irq_to_spi_max_us` above the 125 us ADS131 period;
- `drdy_timing_fault` followed by FIFO/SYNC recovery;
- `adc_missed`, under-K windows, and partial output frames;
- occasional `sched_abort_post`.

The V15 periodic health line was approximately 900–1000 characters and was printed every two seconds after any cumulative fault became nonzero. With a 115200-baud console this is a large blocking output operation. The old condition used cumulative counters, so one early fault enabled repeated long warning lines for the rest of the session.

V16 makes runtime diagnostics **pull-based through UDP**, not push-based through the serial console.

## 2. Runtime serial quiet mode

### Behavior

Immediately before `start_fnirs_acquisition()` enables the ADS131 producer and optical scheduler, V16 lowers these log tags to `ESP_LOG_ERROR`:

- `app_fnirs_test`
- `nocti_wifi`
- `nocti_timebase`
- `nocti_sched`
- `nocti_fnirs`
- `nocti_led`
- `nocti_hal_i2c`
- `nocti_hal_spi`
- `nocti_stream`
- `nocti_board`
- ESP-IDF `wifi`

The previous per-tag levels are saved with `esp_log_level_get()` and restored only after acquisition has fully stopped.

### Result

During `START` → `STOP`:

- INFO logs are suppressed;
- WARN logs are suppressed;
- ERROR logs remain visible;
- UDP control and binary stream continue normally;
- `STATUS` remains available through UDP;
- no periodic health string is formatted or written to stdout.

The `START` UDP reply now includes:

```text
serial_quiet=1
```

## 3. Periodic health serial output removed

The previous two-second block that collected every subsystem statistic and emitted one very long `NOCTI_LOGW()` line was removed.

The main loop now only increments:

```text
health_suppressed
```

once every 10 seconds while acquisition is active. This is a diagnostic indication that a periodic serial health opportunity was intentionally skipped; it performs no large formatting and no console write.

Current health information is still available through:

```text
STATUS
```

The UDP `STATUS` response adds:

```text
serial_quiet=<0|1>
health_serial=0
health_suppressed=<count>
trace_events=<count>
trace_capacity=128
```

## 4. Compact STOP-only serial summary

V16 restores normal log levels only after:

1. `g_run` is cleared;
2. GPTimer scheduler alarm is stopped;
3. PCA9956B is commanded all-off;
4. pending processing jobs are drained or bounded by timeout;
5. ADS131 DRDY capture and consumer acquisition are stopped.

It then prints a compact summary. Example healthy result:

```text
I (...) app_fnirs_test: STOP summary session=1 frames=600 partial=0 underk_on=0 underk_off=0 adc_ok=... adc_err=0 missed=0 crc_bad=0 trace=2
I (...) app_fnirs_test: STOP timing clean irq2spi_max_us=... spi_max_us=... tb_late_max_us=...
```

If faults occurred, the second line is a concise warning containing timing fault, recovery, maximum IRQ-to-SPI latency, maximum SPI transfer time, scheduler aborts, and maximum scheduler callback time.

Because acquisition is already stopped, serial blocking at this point cannot invalidate optical data.

## 5. RAM diagnostic trace

V16 adds a 128-entry lock-free diagnostic ring. It records only exceptional or boundary events and does not print them during acquisition.

Recorded event types:

- `START`
- `ADC_ERR`
- `ADC_GAP`
- `SCHED_ABORT`
- `STOP`

Each entry contains:

- monotonically increasing trace sequence;
- Nocti GPTimer timestamp;
- event type;
- two event-specific integer values.

### New command

After `STOP`, send:

```text
TRACE
```

The firmware returns the latest 32 committed events over UDP. `TRACE` is rejected while acquisition is running to avoid formatting a long response during the real-time interval.

For `ADC_GAP`:

```text
v0 = logical conversion step
v1[31:16] = raw IRQ step
v1[15:0]  = timestamp-derived step
```

For `SCHED_ABORT`:

```text
v0 = scheduler abort reason
v1 = scheduler slot index
```

## 6. Console configuration

The project keeps the primary UART console at 115200 baud, but disables the secondary USB Serial/JTAG console:

```text
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
# CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG is not set
```

This avoids duplicating each application log to two console sinks. `CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=y` is explicitly retained in `sdkconfig.defaults` because runtime quiet mode requires per-tag level changes.

No baud-rate change was made, so the existing serial-monitor workflow remains compatible.

## 7. Timebase getter fix included

The local V15 two-line correction is included in V16:

```c
out_status->init_core_id = s_status.init_core_id;
out_status->alarm_isr_core_id = s_status.alarm_isr_core_id;
```

Without these assignments, the public status structure remained zero-initialized and could incorrectly report `tb_init_core=0`, causing core-plan validation to reject `START` even though GPTimer was actually allocated on core 1.

## 8. Unchanged acquisition parameters

V16 intentionally does **not** hide timing faults by relaxing scientific requirements. These remain unchanged:

```text
ADS131M04 data rate       = 8 kSPS
ADS131 SPI clock          = 8 MHz
SPI mode                  = mode 1
DMA                       = off
Target K                  = 10 ON / 10 I_LED_OFF
Minimum-valid K           = 8 ON / 8 I_LED_OFF
Optical slot              = 5 ms
LED ON/OFF deadline       = 2.5 ms / 2.5 ms
Optical settling          = 500 us
PCA9956B I2C              = 400 kHz
Single GPTimer            = retained
Core 1                    = GPTimer, DRDY, ADC consumer, scheduler, LED I2C
Core 0                    = processing and Wi-Fi
```

The v16 test therefore isolates the effect of serial-console activity without changing ADC, LED, K, timing tolerance, or optical-window criteria.

## 9. Files changed

```text
main/app_main_fnirs_test.c
main/CMakeLists.txt
components/nocti_timebase/nocti_timebase.c
components/nocti_common/include/nocti_version.h
sdkconfig
sdkconfig.defaults
NOCTINIRS_FIRMWARE_REVISION_v16.md
```

## 10. Build procedure

From the ESP-IDF 5.5.1 terminal:

```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

`menuconfig` is not required when the supplied `sdkconfig` is used.

## 11. Recommended A/B validation

Use the same hardware placement, LED currents, SoftAP client, and recording duration used for V15.

### V16 test

```text
MODE SCAN
CURRENT 735 30
CURRENT 850 30
START
```

Do not repeatedly request `STATUS`. Let it run for at least 60 seconds, then:

```text
STOP
STATUS
TRACE
```

### Primary pass targets

```text
partial=0
underk_on=0
underk_off=0
adc_err=0
adc_missed=0
adc_period_gap=0
drdy_timing_fault=0
fifo_recovery=0
sched_abort=0
crc_bad=0
drdy_overflow=0
process_drop=0
process_wait_timeout=0
tb_catchup=0
tb_read_err=0
tb_rearm_err=0
```

### Latency targets to compare against V15

```text
irq_to_spi_max_us   substantially below the V15 maximum of 384 us
sched_cb_max_us     below the late-completion admission boundary
```

If V16 becomes clean, serial logging was a significant contributor. If faults remain with serial quiet, `TRACE` will help localize the remaining issue without injecting runtime console traffic. The next investigation should then focus on core-1 interrupt/critical-section latency around PCA9956B I2C completion and ADS131 SPI service rather than weakening DRDY validation.

## 12. Validation performed in the preparation environment

- C syntax check for `app_main_fnirs_test.c` using Clang and ESP-IDF interface stubs: passed.
- C syntax check for `nocti_timebase.c`: passed.
- `printf`-format validation for control responses using Clang `-Wformat=2`: passed.
- Verified runtime serial quiet keeps `ESP_LOG_ERROR` enabled.
- Verified `TRACE` cannot be dumped while acquisition is active.
- Verified secondary USB Serial/JTAG console is disabled in `sdkconfig`.
- Verified firmware version string is `0.16.0`.

A full Xtensa ESP-IDF build and physical ESP32-S3 test were not available in the preparation environment and must be completed on the NoctiNIRS hardware.
