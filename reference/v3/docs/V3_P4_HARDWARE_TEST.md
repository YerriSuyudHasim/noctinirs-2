# Prompt 4 Hardware Validation Plan

No hardware results are included in this archive.

## A. Build and boot gate

1. Use exact ESP-IDF 5.5.1.
2. `idf.py set-target esp32s3 && idf.py fullclean && idf.py build`.
3. Review all warnings and linker map.
4. Confirm IRAM/internal-DRAM placement for DRDY, GPTimer, I2C callback, and
   hot rings.
5. Confirm GPIO35–37 compatibility for the exact module/schematic.

## B. DARK acquisition

Run 10 minutes with quiet Wi-Fi and 10 minutes with active UDP control.
Expected clean counters: missed/gap/CRC/status/SPI/ring overflows all zero.

## C. SCAN classification

Run at 400 kHz I2C first:

- query `HEALTH` every five seconds;
- capture `DUMP_SLOT_TIMING`;
- export frame target/minimum/partial/invalid distributions;
- confirm K target is treated as target, not assumed guarantee;
- verify stable ON and `I_LED_OFF` counts against raw timestamps;
- verify exact half-open classification at observed boundaries.

## D. Frame ownership

- pause the future frame consumer to exercise the 16-frame ring;
- confirm only `completed_frame_ring_overflow`/drop changes;
- confirm no raw-ring corruption;
- restart session and prove no old session/config frame is exposed.

## E. Lifecycle and faults

Run `V3_P4_LIFECYCLE_TEST.md`, then inject NACK, timeout, CRC, DRDY-stop, and
scheduler-delay faults. Verify reason codes and LED OFF before STOPPED/FAULTED.

## F. Experimental 1 MHz profile

Only after 400 kHz acceptance, select the experiment build, capture SCL/SDA
waveforms and callback latency distributions, and do not promote it to default
without repeated clean runs.
