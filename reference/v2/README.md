# NoctiNIRS fNIRS validation firmware — v15

Target: ESP32-S3, ESP-IDF 5.5.1. The active application is
`main/app_main_fnirs_test.c`.

This revision keeps the existing `paired_v2_4_8k` wire protocol and optical
configuration, but replaces the ADS131M04 DRDY handling with a deterministic
continuous event engine. No live-view binary record layout change is required.

## Acquisition configuration

```text
ADS131M04 data rate       8000 SPS (125 us/sample)
ADS131 SPI                8 MHz, mode 1, polling, DMA off
Optical frame rate        10 Hz
Slots per frame           20
Slot period               5000 us
LED-ON / LED-OFF budget   2500 / 2500 us
Settling guard            500 us
Target window             1250 us
Minimum-valid window      1000 us
Target K                  10 ON / 10 LED-OFF
Minimum-valid K           8 ON / 8 LED-OFF
PCA9956B I2C              400 kHz, asynchronous runtime fast path
```

`I_LED_OFF` means LED-off intensity. It may contain photodiode dark current,
electronic offset, ambient light, and other LED-off contributions.

## v15 core plan

```text
Core 1 — deterministic acquisition
  GPTimer allocation and alarm ISR
  GPIO ISR service and ADS131 DRDY ISR
  ADS131 continuous read task, priority 18
  optical scheduler, priority 17
  PCA9956B asynchronous I2C controller/callback allocation

Core 0 — noncritical work and transport
  app/control path
  fNIRS averaging and frame assembly, priority 11
  Wi-Fi control, advertisement, and UDP transmit tasks
```

The firmware reports the observed ISR/task core IDs in `STATUS`; do not rely on
this table alone when validating a build.

## ADS131M04 event engine

Every accepted ADC frame is paired with exactly one falling-edge event captured
inside the GPIO ISR:

```text
DRDY falling edge
  -> ISR timestamp + IRQ sequence -> static SPSC event ring
  -> permanent ADC consumer task
  -> one complete 18-byte SPI frame under one CS assertion
  -> CRC/status parsing
  -> committed ADC history entry
```

The event ring preserves timestamps but does not extend the ADS131M04 physical
two-sample FIFO. The driver performs a bounded non-reset SYNC recovery when:

- event backlog exceeds two samples;
- IRQ/timestamp spacing is not exactly one conversion period;
- an SPI transaction fails and the data/FIFO epoch becomes ambiguous.

A recovered or missing conversion remains visible as a gap. The collector never
fills it with older data.

## Build and flash

```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

## First validation

Boot must complete the 1024-frame active-read test at 8 kSPS. Then run:

```text
MODE SCAN
START
STATUS
```

Observe for at least 60 seconds. Healthy targets are:

```text
adc_sps_validated=7600..8400
crc_bad=0
adc_missed=0
adc_irq_dup=0
adc_ts_nonmono=0
adc_period_gap=0
drdy_overflow=0
fifo_risk=0
drdy_timing_fault=0
spi_recovery=0
fifo_recovery=0
underk_on=0
underk_off=0
partial=0
led_err=0
sched_missed=0
sched_abort=0
```

Expected critical core IDs after acquisition has started:

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

See `NOCTINIRS_FIRMWARE_REVISION_v15.md` for the full source review, design
rationale, file-by-file changes, and validation checklist.
