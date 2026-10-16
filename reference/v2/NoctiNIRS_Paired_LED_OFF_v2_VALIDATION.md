# NoctiNIRS Paired LED-OFF v2 — Revision and Validation Report

Date: 2026-07-17

## Root cause

The previous live-view workflow automatically started a standalone `MODE DARK` session to create an LED-off profile. DARK mode intentionally leaves every LED off. When one or more slots failed the fixed-K validity requirement, the software aborted the LED-off profile and never advanced to `MODE SCAN`. Consequently, acquisition appeared active but the LEDs never illuminated.

The previous frame payload also contained only one intensity matrix. It therefore could not guarantee that `I_LED_ON` and `I_LED_OFF` came from the same optical frame and local time interval.

## Implemented acquisition architecture

The ADC runs continuously at the configured 4 kSPS. Each 5 ms optical slot now contains a local paired measurement:

1. The previous LED is OFF.
2. Ignore at least the first 1000 us after LED-OFF.
3. Average the newest contiguous five CRC-valid LED-OFF conversions.
4. Turn on the current LED.
5. Ignore the first 1000 us after LED-ON.
6. Average the newest contiguous five CRC-valid LED-ON conversions from a 1400 us window.
7. Turn the LED OFF and begin the next local OFF interval.

The slot budget is:

- slot period: 5000 us
- LED-ON budget: 2400 us
- LED-ON settling guard: 1000 us
- LED-ON sampling window: 1400 us
- nominal LED-OFF gap: 2600 us
- minimum LED-OFF settling guard: 1000 us
- K_ON = K_OFF = 5

Five samples were selected instead of six to preserve a stronger timing margin after ADC digital-filter settling, I2C transaction latency, task scheduling jitter, and DRDY phase alignment. A sixth sample would only improve uncorrelated averaging noise by a factor of sqrt(6/5), approximately 1.095, while materially reducing the safety margin.

## Pairing and validity rules

Firmware protocol v2 transmits two phase records with the same `frame_id`:

- LED-ON phase
- LED-OFF phase

The bridge pairs them only when both the UDP acquisition `session_id` and fNIRS `frame_id` match. It never reuses an earlier LED-OFF value.

A slot is accepted only when:

- both phases are present;
- `n_used_on = 5` and `n_used_off = 5` when v2 timing metadata is available;
- ON and OFF sample counts are equal;
- both phase records mark the slot valid;
- neither phase reports UNDER-K;
- neither phase reports a CRC-dropped conversion;
- all four detector averages are finite.

The corrected signal remains signed:

`I_corrected = I_LED_ON - I_LED_OFF`

No absolute-value operation or stale-value fallback is used. A nonpositive corrected intensity is invalid for OD and must be investigated at the hardware/coupling level.

## Firmware protections

- ADS131M04 CLOCK register is checked against the expected 4 kSPS configuration.
- A 512-frame active-read validation synchronizes and drains the ADS131M04 continuously; the measured rate must be 3800–4200 SPS with zero CRC failures, sequence gaps, or duplicate DRDY sequences.
- The collector uses only one contiguous block of the newest K samples.
- A CRC failure or DRDY sequence gap terminates the candidate block; older samples are not used to fill the window.
- Any missing, UNDER-K, CRC-affected, scheduler-aborted, or phase-incomplete slot marks both records partial.
- LED-driver failure forces all outputs OFF and stops acquisition.
- Runtime STATUS reports separate ON/OFF quality counters and timing diagnostics.

## Software behavior

- Start acquisition now sends `STOP`, the selected `MODE`, currents, `START`, and `STATUS` directly.
- SCAN and SINGLE do not run a preliminary DARK session.
- DARK remains an explicit diagnostic mode and intentionally keeps LEDs off.
- The bridge accepts and combines paired protocol-v2 records before broadcasting a corrected frame.
- The UI displays paired validity and separate ON/OFF UNDER-K counts.
- Baseline, OD, MBLL, and short-separation processing receive only a valid paired corrected intensity when LED-off correction is enabled.

## Completed validation

### Software

`npm run check` passed, including:

- JavaScript syntax checks;
- geometry mapping tests;
- OD and MBLL tests;
- short-separation tests;
- paired-frame tests for strict K, unequal counts, CRC rejection, frame mismatch, and missing config behavior.

A local bridge/simulator/WebSocket integration test passed with:

- protocol version 2;
- K_ON = K_OFF = 5;
- 20/20 valid slots;
- no partial flag;
- `n_used_on = n_used_off = 5`;
- exact equality between broadcast corrected data and `LED_ON - LED_OFF`.

### Firmware source

- Clang C syntax, warning, and printf-format check passed using ESP-IDF interface stubs.
- Packed protocol size checks passed: fNIRS config v2 = 16 bytes and phase-frame payload = 356 bytes.

A real ESP-IDF build, flash, and hardware run could not be performed in this environment because the ESP-IDF toolchain and physical NoctiNIRS board are unavailable. The source package intentionally excludes the old `build/` directory so that no stale Windows binary is mistaken for the revised firmware.

## Hardware acceptance checklist

After flashing, select SCAN and start acquisition. Expected indicators:

- startup confirms `ADS131=4000 SPS` and paired K(on/off)=5/5;
- START log reports `mode=SCAN`, not DARK;
- `protocol=paired_v2`;
- `adc_sps=4000`;
- `min_n_on=5`, `min_n_off=5` after frames begin;
- `underk_on=0`, `underk_off=0`;
- `partial=0`;
- `crc_bad=0`;
- `led_err=0`;
- `sched_abort=0` and `sched_overrun=0`;
- UI shows `Paired 20/20 · UK 0/0`;
- LEDs illuminate sequentially in SCAN mode.

Investigate rather than masking the problem if any of these fail. In particular, do not lower K or take an absolute value until DRDY rate, I2C timing, LED mapping/current, detector polarity, TIA saturation, and optode coupling have been checked.
