# Prompt 4 START/STOP 100-Cycle Test Procedure

Hardware success is not claimed by this document. Run only after a clean
ESP-IDF 5.5.1 build, GPIO35–37 compatibility confirmation, and basic DARK/SCAN
sanity checks.

## Preconditions

1. Confirm exact ESP32-S3 module and NoctiBoard schematic permit ADS GPIO35/36/37.
2. Verify PCA `RESET#` forces all outputs OFF.
3. Confirm ADS startup reports valid 8 kSPS rate and clean register readback.
4. Confirm I2C default is 400 kHz and MODE2 reports change-on-STOP.
5. Start with counters reset while stopped.

## Automated cycle

Repeat 100 times:

1. Send `MODE SCAN`; verify reply says `applies=next_start`.
2. Send `START`; require `START_OK` and a new session epoch.
3. Run for at least 1.2 s (at least 12 nominal frames).
4. Query `STATUS`, `HEALTH`, and optionally `DUMP_SLOT_TIMING`.
5. Send `STOP`; require bounded `STOP_OK` without a fixed-delay workaround.
6. Query `STATUS`; require `state=STOPPED`, `running=0`, `scan_armed=0`, LED safe/OFF.
7. Wait only for the command acknowledgement/transport interval, not as a
   lifecycle barrier.

Every tenth cycle, generate UDP control traffic while SCAN runs. At cycles
25/50/75, request STOP near a different optical phase (ON collect, OFF collect,
guard) using host timing variation.

## Per-cycle assertions

- session epoch increases exactly once per successful START;
- config generation increases exactly once per requested session;
- no completed frame from the previous session is exposed as current;
- all STOP acknowledgements follow force-OFF and ADC/process/frame-sink
  quiescence;
- no task or callback accesses a deleted object;
- actual task/callback cores remain expected;
- stack watermarks remain above the project acceptance margin;
- no DRDY/raw/timing/completion ring overflow;
- no repeated stale callback;
- no unbounded recovery loop.

## End-of-test acceptance record

Capture final `HEALTH` and store:

```text
100 START_OK
100 STOP_OK
adc_missed=0
adc_sequence_gap=0
drdy_event_overflow=0
adc_ring_overflow=0
crc_bad=0
spi_errors=0
i2c_completion_ring_overflow=0
stale_i2c_completion=0
scheduler_event_overflow=0
timing_ring_overflow=0
completed_frame_ring_overflow=0
state=STOPPED
LED verified OFF
minimum stack watermark for every application task
```

Under-K may occur during deliberately phase-stressed STOP cycles, but must be
contained to partial/invalid final frames and never leak into the next session.

## Fault-injection extension

After the clean 100-cycle run:

- inject one I2C NACK and verify RECOVERING→STOPPED with LED OFF;
- inject repeated I2C timeout and verify FAULTED/session stop;
- interrupt DRDY for >5000 us and verify ADC_DRDY_STOPPED;
- inject three CRC failures and verify session stop;
- stall the frame consumer to verify counted frame drop without raw acquisition
  corruption.
