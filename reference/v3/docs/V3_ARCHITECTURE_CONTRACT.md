# NoctiNIRSv3 Architecture Contract

**Contract version:** 1.4 — Prompt 5 final-integration amendment  
**Target:** ESP32-S3, ESP-IDF 5.5.1, FreeRTOS SMP  
**Firmware identity:** `NoctiNIRSv3 3.4.0 P5_FINAL_INTEGRATION`

This document is normative for the delivered source. Timing, object ownership,
packet layout, or lifecycle policy may change only through an explicit contract
amendment and version update.

## 1. Non-negotiable system contract

- ESP32-S3 dual core; ESP-IDF 5.5.1; C only; no Arduino API.
- BLE excluded from the fNIRS validation firmware.
- ADS131M04: SPI3_HOST, mode 1, 8 MHz, 24-bit, four simultaneous channels,
  target 8 kSPS, nominal DRDY 125 us.
- Optical target: 20 slots × 5000 us = 100000 us/frame = 10 Hz.
- K target ON/`I_LED_OFF` = 10/10; minimum-valid = 8/8.
- PCA9956B address comes only from board configuration.
- PCA9956B uses external I2C pull-ups, atomic six-LEDOUT register burst, and
  change-on-STOP output update.
- I2C 400 kHz is the default. 1 MHz is an explicit experimental build profile.
- PCA9956B RESET# remains the hardware fail-safe OFF mechanism.
- Use `I_LED_OFF`; host wavelength naming remains `CHx_I735`, `CHx_I850`,
  `CHx_HbO`, and `CHx_HbR`.
- MBLL, SSR, baseline, bandpass, and UI remain outside the hard-real-time path.
- GPIO35/36/37 compatibility with the exact N16R8 module is a hardware gate.

## 2. Single source of timing and task configuration

All timing, K, bus, task, ring, threshold, and cooldown values are defined in:

```text
components/nocti_v3_common/include/nocti_v3_config.h
```

Active timing:

```text
frame / slot                   100000 / 5000 us
slots / frame rate             20 / 10 Hz
nominal LED-OFF                slot start + 2500 us
settle ON / I_LED_OFF          500 / 500 us
end guard                      400 us
target windows                 1250 / 1250 us
minimum windows                1000 / 1000 us
ADS rate / period              8000 SPS / 125 us
K target / minimum             10/10 / 8/8
I2C default / experiment       400 kHz / 1 MHz
commit design allowance        250 us; hardware measurement pending
```

No command may change active timing in the middle of a slot. Mode and current
changes are immutable next-start configuration.

## 3. Component boundaries

| Component | Sole responsibility | Explicit exclusions |
|---|---|---|
| `nocti_v3_common` | version, config, lifecycle/fault enums | hardware access |
| `nocti_v3_board` | pins, early fail-safe, persistent bus/device handles | scheduler policy |
| `nocti_v3_timebase` | common 64-bit 1 MHz time and absolute deadline event | averaging/network |
| `nocti_v3_adc` | DRDY ISR, one ADS frame/event, rate/integrity, raw SPSC producer | optical classification |
| `nocti_v3_led` | PCA init, persistent payloads, async submit/completion, recovery | frame assembly |
| `nocti_v3_optical` | slot state machine, timing records, classification, representative values, frame ownership | socket I/O, MBLL/SSR |
| `nocti_v3_stream` | text commands and stream-v1 encoder/layout | socket ownership |
| `nocti_v3_wifi` | SoftAP, command task, UDP stream/drop policy | RT state ownership |
| `nocti_v3_health` | synchronized system/network snapshot | data-path synchronization |

Legacy and new ESP-IDF I2C drivers must not coexist.

## 4. End-to-end ownership

```text
DRDY ISR -> ADS task -> raw SPSC -> optical task
          -> completed-frame SPSC -> UDP stream task -> UDP record
```

| Shared object | Producer | Consumer | Full/slow policy |
|---|---|---|---|
| DRDY event ring | ISR | ADS task | no overwrite; integrity fault |
| raw sample ring | ADS task | one mode-specific consumer | no overwrite; session fault |
| LED request queue | scheduler | LED task | invalidate/recover |
| I2C completion ring | callback | scheduler | reject/count overflow/stale identity |
| slot timing ring | scheduler | optical task | invalidate timing/frame |
| completed-frame ring | optical task | UDP task | drop/count; acquisition continues |
| UDP frame copy/buffer | UDP task | lwIP call | task-owned persistent copy |

SPSC publication writes the record then release-stores `head`; consumption
acquire-loads `head`, copies the record, then release-stores `tail`.

## 5. Final task/priority plan

```text
core 1: ADC 20 > scheduler 19 > LED 18 > recovery 10
core 0: optical 12 > command 9 > UDP/frame sink 8 > health 5
```

All application tasks are explicitly pinned; application `NO_AFFINITY` is
forbidden. Runtime actual cores and stack watermarks are exported in HEALTH.

## 6. ISR/callback rules

DRDY, GPTimer, and I2C completion callbacks may only:

- capture the common timestamp and compact immutable identity;
- update small ISR-safe counters/rings;
- notify a task;
- return.

They may not log, allocate, block, perform SPI/I2C readback, reset hardware,
assemble frames, average, or call networking APIs. Completion timestamps are
conservative post-transaction boundaries, not direct optical-edge measurements.

## 7. Classification and representative values

Stable LED ON:

```text
drdy_ts >= led_stable_start_ts
AND drdy_ts < led_off_request_ts
```

Stable `I_LED_OFF`:

```text
drdy_ts >= led_off_stable_start_ts
AND drdy_ts < slot_guard_start_ts
```

All windows are half-open. Transition, settling, guard, stale epoch, recovery,
uncommitted, and ambiguous samples are rejected and counted.

Representative value: arithmetic mean of the newest contiguous integrity-valid
tail, maximum K=10. Search stops at SPI, CRC, ADS status, sequence, or epoch
break; no older backfill and no duplicated samples.

`I_corrected = I_LED_ON - I_LED_OFF` uses a 64-bit intermediate and saturates to
signed 32-bit.

## 8. Slot/frame quality

```text
VALID_TARGET   n_on >=10 and n_off >=10
VALID_MINIMUM  n_on >=8 and n_off >=8, with one/both below 10
PARTIAL        timing valid and some usable data, but one/both below 8
INVALID        timing invalid, scratch overflow, missing/duplicate slot, or no usable windows
```

A valid frame requires all 20 slots target/minimum-valid and all ten 735/850
pairs present. Completed frames contain copied slot/pair data and never export a
pointer into mutable scratch.

## 9. Network contract and backpressure

- UDP control port 3333; binary stream destination port 3334.
- Command and stream tasks are non-real-time on core 0.
- Stream socket uses nonblocking send.
- No mutex is held during `sendto`.
- Missing peer, network not ready, disabled stream, encoding error, `EAGAIN`, or
  partial/failed send drops that network frame and increments counters.
- No unbounded retry is permitted; acquisition continues.
- `udp_peer_active` is configured by a command sender and is not cleared solely
  by command silence.

The stream-v1 12-byte envelope, record IDs, CRC behavior, and v16 payload
layouts are retained. Extensions require payload versioning or a new record ID.

## 10. Lifecycle and configuration

Persistent objects/tasks are created once at startup and are not deleted per
session.

START:

1. require stopped/safe/ready state;
2. reset and acknowledge stream consumer;
3. select immutable next configuration and increment generation;
4. clear/acknowledge ADC and processing ownership;
5. enable acquisition and schedule a new absolute frame origin;
6. enable stream only after START succeeds.

STOP:

1. reset/disable stream delivery;
2. reject new frame/slot generation and invalidate epochs;
3. force LED OFF and quiesce pending I2C with bounded acknowledgement;
4. stop/drain ADC;
5. finalize/flush truncated old-session frame state;
6. return stopped acknowledgement.

STOP is idempotent. START in an incompatible state returns an explicit error.
No fixed delay substitutes for lifecycle acknowledgement.

## 11. Fault classes

Recoverable examples: one under-K slot, single SPI/CRC/status failure, single
I2C NACK, completed-frame/network pressure. Fatal/session-stop examples: DRDY
silence, ring corruption/overflow in RT paths, repeated CRC/SPI/status/I2C or
scheduler misses, repeated stale callback violation, or LED cannot be verified
OFF. Thresholds and actions are documented in `V3_FAULT_RECOVERY_TABLE.md`.

## 12. Allocation and logging

- No application heap allocation is allowed after START.
- Hot rings, frames, async buffers, commands, and packet buffers are static or
  persistent internal objects.
- No per-sample or normal per-slot log.
- Periodic health logging runs at the lowest application priority every 5 s.

## 13. Validation classification

The package must report four independent states:

- static review verified;
- build verified;
- hardware test partially verified;
- acceptance verified.

Only the first is verified by the generation environment. A successful target
build alone is not production or timing acceptance.
