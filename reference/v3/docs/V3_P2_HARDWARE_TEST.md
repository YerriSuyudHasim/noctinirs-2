# Prompt 2 Hardware Test Procedure

No physical test was run in the archive-generation environment. This procedure
is the required validation sequence.

## 1. Build and preflight

1. Confirm `idf.py --version` is ESP-IDF v5.5.1.
2. Run `idf.py fullclean build` and archive the complete build log.
3. Before flashing, identify the exact ESP32-S3 chip/module and inspect the
   schematic. If GPIO35/36/37 are physically connected to Octal PSRAM, stop:
   the retained ADS pin map is unsupported even when `CONFIG_SPIRAM` is off.
4. Flash and monitor.
5. Confirm PCA9956B RESET# GPIO18 remains LOW with a scope/logic analyzer.
6. Confirm boot prints:
   - SPI3, mode 1, 8 MHz, 18-byte frame;
   - register readback `CLOCK=0x0F0A`, `GAIN=0x0000`, `CFG=0x0600`;
   - measured DRDY within 7600–8400 SPS;
   - ADC ready.
7. Send `REGS`, `TIMING`, `STATUS`, and `HEALTH` over UDP port 3333 and save
   replies.

## 2. DARK test A — Wi-Fi application traffic quiet, 10 minutes

Wi-Fi may remain initialized for control, but do not poll commands during the
run except start and final health/stop.

```text
RESET_COUNTERS
MODE DARK
START
(wait 10 minutes)
HEALTH
STOP
HEALTH
```

Acceptance:

```text
adc_missed=0
adc_sequence_gap=0
drdy_event_overflow=0
adc_ring_overflow=0
crc_bad=0
ads_status_errors=0
spi_errors=0
integrity_fault=0
adc_core=1
isr_callback_core=1
PCA RESET# remains LOW
```

Also record period min/max/average, maximum gap, maximum SPI duration, event/raw
ring depths, and both stack watermarks.

## 3. DARK test B — Wi-Fi + UDP load, 10 minutes

Start a second ten-minute DARK session. During the run, poll `STATUS`, `HEALTH`
and `TIMING` at 2–5 Hz from the host. Do not request raw streaming because P2
intentionally has none.

Use the same zero-error acceptance criteria. Any missed/event/ring/CRC/status or
SPI error fails the phase and must be investigated before Prompt 3 timing claims.

## 4. Logic-analyzer capture

Capture at least:

- ADS DRDY;
- ADS CS;
- ADS SCLK;
- MOSI/MISO if channel count permits;
- PCA RESET#.

Verify:

- DRDY nominal period near 125 us;
- SPI mode 1 and SCLK near 8 MHz;
- one CS-low 18-byte frame per serviced DRDY;
- CS returns high before the next DRDY;
- no ADS SPI activity while STOPPED;
- PCA RESET# never releases in P2.

## 5. Evidence to preserve

- firmware ZIP SHA-256;
- full build output;
- boot log;
- UDP replies before/during/after both tests;
- logic-analyzer file/screenshots;
- exact sdkconfig;
- board and ESP32-S3 module revision.
