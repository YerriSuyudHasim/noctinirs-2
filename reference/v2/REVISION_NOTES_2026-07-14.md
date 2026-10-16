# NoctiNIRS fNIRS timing revision — 2026-07-14

This source revision applies the scheduler/LED-driver corrections agreed during the ADS131M04–PCA9956B timing review.

## Changes

1. **ESP32-S3 I2C clock**
   - `main/app_main_fnirs_test.c`
   - Changed PCA9956B bus clock from `1,000,000 Hz` to `400,000 Hz`.
   - Internal pull-ups remain enabled as a supplementary fallback; the board should still use suitable external pull-ups for reliable 400 kHz operation.

2. **PCA9956B output delay**
   - `components/nocti_led_driver/nocti_led_driver.c`
   - Added register `OFFSET (0x3A)` initialization to `0x00` so output transitions are not staggered by PCA channel number.
   - `MODE2 = 0x00` is retained, so output changes latch on the I2C STOP condition.

3. **Scheduler timing semantics**
   - Removed the misleading `on_time_us` field from `nocti_slot_t`.
   - `SLOT_END` now occurs unambiguously at:
     `slot_start + settle_us + sample_window_us`.
   - The `SLOT_END` callback is both the ADC-window end and the physical LED-OFF command.
   - Current timing is:
     - slot start / LED ON command: `0 us`
     - candidate ADC window start: `200 us`
     - LED OFF / candidate window end: `3400 us`
     - guaranteed OFF gap: `1600 us`
     - next slot: `5000 us`

## Important build note

The original ZIP contained build artifacts produced before these source changes. They were removed from this revised package to prevent accidentally flashing a stale binary. Run a clean ESP-IDF build before flashing:

```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

Target environment: ESP-IDF 5.5.1, ESP32-S3.
