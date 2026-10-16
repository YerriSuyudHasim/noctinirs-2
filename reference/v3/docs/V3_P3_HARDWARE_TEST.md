# Prompt 3 Hardware Test Procedure

No hardware log was available when this archive was generated.

## 0. Preflight gate

1. Confirm exact ESP32-S3 module part number and schematic.
2. Confirm GPIO35/36/37 are not physically occupied by Octal PSRAM.
3. Use external SDA/SCL pull-ups and default 400 kHz profile.
4. Attach logic analyzer to SDA, SCL, PCA RESET#, ADS DRDY, ADS CS/SCLK, and an
   observable LED/photodiode signal where safe.

## 1. Build/boot

```bash
idf.py --version          # exactly v5.5.1
idf.py fullclean build flash monitor
```

Require:

- ADS measured rate accepted near 8 kSPS;
- PCA address detected from board config;
- MODE1/MODE2 readback valid;
- all six LEDOUT registers read back OFF;
- I2C init core = 1; scheduler/LED/ADC tasks = core 1;
- no unknown Kconfig/API/link warnings.

## 2. Short SCAN diagnostic

Send:

```text
MODE SCAN
START
STATUS
HEALTH
DUMP_SLOT_TIMING
STOP
```

Verify on analyzer:

- 20 one-hot slot commands per 100 ms frame;
- all six LEDOUT registers update under one STOP-committed transaction;
- no two LED outputs are active together;
- nominal slot cadence does not drift from the frame origin;
- RESET# stays released during healthy scan and asserts immediately on injected
  LED-bus fault.

## 3. Ten-minute acceptance runs

Run separately:

1. SCAN, Wi-Fi traffic quiet, 10 minutes.
2. SCAN, Wi-Fi connected with repeated STATUS/HEALTH/TIMING queries, 10 minutes.

Target counters at end:

```text
adc_missed=0
adc_sequence_gap=0
drdy_event_overflow=0
adc_ring_overflow=0
crc_bad=0
spi_errors=0
i2c_nack=0
i2c_timeout=0
i2c_queue_full=0
i2c_completion_ring_overflow=0
stale_i2c_completion=0
sched_missed=0
sched_abort=0
scheduler_event_overflow=0
```

Record, do not assume:

- `led_commit_latency_max_us` and distribution;
- `scheduler_lateness_max_us`;
- minimum ON and `I_LED_OFF` stable windows;
- sample counts per timing window;
- all task/callback core IDs and stack watermarks.

## 4. Recovery test

Using a safe controlled fault method, create one I2C NACK/timeout. Verify:

1. current slot/session becomes invalid;
2. PCA RESET# asserts immediately;
3. no normal slot command continues;
4. recovery task performs bounded reinit/readback;
5. session does not resume mid-frame;
6. repeated faults stop after the configured threshold and do not reset-loop.

## 5. 1 MHz experiment

Only after the 400 kHz profile passes, create a separate build/profile for
1 MHz. Capture rise/fall time, ACK/NACK, duty cycle, clock stretching, and PCA
output timing. Do not merge it into the default profile without evidence.
