# V4 IDF 6.1 single clock: the ADS131M04 sample index

Status: design accepted for implementation by the Owner on 2026-10-29 at 23:54 KST
("Oke aku setuju, lanjutkan PR berikutnya sesuai ide mu"). This file is the
handoff. A successor who has only this repository must be able to finish any
unchecked box without another conversation.

Evidence class of this document: static design. Host unit tests are named in
the checklist and are Class U when they have actually been run. Every on-target
item is **NOT RUN**. Nothing here authorizes flash, a logic analyzer session,
energized emitters, a merge, a tag, or a release.

Scope: `firmware/v4-idf61` on branch `cursor/n3-single-clock-72a4`, stacked on
draft PR #77 (`cursor/n3-d6-brownout-d633`, D6 emitter-off + brownout), which
itself sits on `feat/35-v4-idf61-restart` containing the PR #76 RTOS work
(`4b91e91`). Base of the pull request is `feat/35-v4-idf61-restart`. After #77
merges, rebase this branch onto the updated `feat/35-v4-idf61-restart` and
retarget. Rebase only. Do not force-push a shared branch.

Out of scope: `reference/**`, GPIO26, eFuse, flash encryption, secure boot,
partition changes, the 30 mA LED cap, the D6 `IREFALL` then `PWMALL` then
`LEDOUT` order, Wi-Fi-only `nn2ctl` control, and the PR #76 task table.

Accountable role: Firmware Developer, with a required RTOS/timing specialist
review (Agent 05) before merge. The author of this change cannot be the
independent reviewer.

## 1. Why the sample index is the master

The ADS131M04 is clocked by its own 8.192 MHz crystal
(`NOCTI_V4_ADS_CLKIN_HZ`, OSR 512). Output data rate is

```text
f_mod = CLKIN / 2 = 4.096 MHz
f_data = f_mod / OSR = 4096000 / 512 = 8000 Hz
T_adc = 125 µs
```

That identity is the `_Static_assert` in
`firmware/v4-idf61/components/nocti_v4_common/include/nocti_v4_ads_config.h`.
DRDY is the end of that conversion. It is the only edge that says "sample n
exists."

The ESP32-S3 clock is a different crystal. A 1 MHz GPTimer
(`GPTIMER_CLK_SRC_XTAL` in `nocti_v4_time.c`) and `esp_timer` both count that
second crystal. Two crystals at tens of ppm diverge by about
0.1 sample per second at 20 ppm, and about 1 sample every few seconds at the
worse end of an untrimmed pair. Over a minute that is tens of samples. LED
edges scheduled on the ESP crystal therefore walk across the ADC grid. Samples
near an edge fall in and out of the µs acceptance window even when the analog
state is stable. That is the likely mechanism of the under-K / spiky data seen
when old feat35 (`31fb2a9`) mixed `esp_timer` (DRDY stamps) with GPTimer (LED
alarms). Do not reintroduce a second clock as a master.

The master timebase is the DRDY sample index `n`, a `uint32_t` that already
increments once per DRDY in the ISR (`drdy_sequence` in `nocti_v4_ads.c`).
Host time for sample `n` is

```text
t_host = t_session_epoch + n / 8000
```

using exact rational arithmetic on the host (`n * 125` microseconds from the
session epoch). Firmware does not convert `n` back onto the ESP crystal to
decide an edge or a class.

### 1.1 What the GPTimer is still for

Keep the 1 MHz GPTimer. It is no longer allowed to arm a LED edge.

| Use | Allowed |
|---|---|
| Lateness metric: ISR stamp of DRDY versus the previous DRDY, reported as a histogram | Yes |
| Diagnostic age of a queued DRDY event (`nocti_v4_backlog.h`, 1 ms) | Yes |
| DRDY-loss watchdog (`NOCTI_V4_ADS_DRDY_STALL_TIMEOUT_US`, 5000 µs) | Yes |
| I2C submit-to-complete latency metric | Yes |
| Arming the next LED ON or OFF | No |
| Classifying a sample into ON, OFF, settle, or guard | No |
| Frame `timestamp_us` | No |

A failed GPTimer read faults the backlog path the way PR #76 already does
(unknown age is a fault). It must not invent a sample index.

## 2. Timing model

### 2.1 Constants

| Name | Value | Source |
|---|---|---|
| `adc_hz` | 8000 | `NOCTI_V4_ADS_SAMPLE_RATE_HZ` |
| `T_adc` | 125 µs | `NOCTI_V4_ADS_EXPECTED_PERIOD_US` |
| Optical `fs` | {2, 4, 5, 8, 10} Hz | `nocti_v4_scheduler_set_fs`. 1 Hz stays an ADR-0002 rate but this image rejects it (ADR-0007). |
| `SLOT_COUNT` | 20 | unchanged |
| `K_target` / `K_min` | 10 / 8 per phase | ADR-0002 R11 |

Slot length in samples, from the current microsecond grid
`slot_us = 1000000 / (20 * fs)`, which is an exact multiple of 125 for every
allowed `fs`:

| `fs` | `slot_us` | `slot_samples` | `frame_samples` | frame period |
|---:|---:|---:|---:|---:|
| 10 | 5000 | 40 | 800 | 100 ms |
| 8 | 6250 | 50 | 1000 | 125 ms |
| 5 | 10000 | 80 | 1600 | 200 ms |
| 4 | 12500 | 100 | 2000 | 250 ms |
| 2 | 25000 | 200 | 4000 | 500 ms |

`frame_samples = slot_samples * 20`.

### 2.2 OFF phase quantized onto the sample grid

The running image stores an OFF phase in microseconds:

```text
default      = slot_us * 44 / 100     /* 2200 µs at 10 Hz */
qualification = slot_us / 2           /* 2500 µs at 10 Hz */
```

2200 / 125 = 17.6 samples. An edge cannot sit between DRDY pulses. Quantize
with round-half-up:

```text
samples = (us + (T_adc / 2)) / T_adc
        = (us + 62) / 125
```

| `fs` | default OFF µs | default OFF samples | qualification samples |
|---:|---:|---:|---:|
| 10 | 2200 | 18 (2250 µs, +50 µs) | 20 |
| 8 | 2750 | 22 | 25 |
| 5 | 4400 | 35 (4375 µs, −25 µs) | 40 |
| 4 | 5500 | 44 | 50 |
| 2 | 11000 | 88 | 100 |

The residual against the historical microsecond constant is at most half a
sample (62.5 µs). It is reported as `off_phase_samples` and is not hidden.
The µs setters stay, so `tools/verify-v4-idf61.py` still sees
`slot * 44 / 100` and `slot / 2`. The sample plan is derived from those
values at SCAN start and then frozen for the session.

ON length is `off_phase_samples`. OFF length is
`slot_samples - off_phase_samples`.

### 2.3 Settle, filter, guard, in samples

TI SBAS890D Rev. D, sections 8.3.7.1.2, 8.3.7.2 and 8.4.2, as already cited by
ADR-0007: the ADS131M04 sinc3 filter needs three output periods to settle
after a step (Equation 6, "three-data-period support"). The group delay is
half of that, 1.5 periods = 187.5 µs. ADR-0004 §3.16.c point 4 says firmware
reports the group delay and does **not** subtract it from `timestamp_us`.
ADR-0004 §3.17 N9 sets

```text
settle >= 3 * T_adc (sinc3) + front-end settling
```

The front-end term is H1 and is **NOT RUN**. Until H1, the placeholder is one
sample (125 µs), which is the ADR-0004 table
(`3 * 125 + 125 = 500` µs). The current macros
`NOCTI_V4_LED_ON_SETTLE_US` and `NOCTI_V4_LED_OFF_SETTLE_US` are 500.

```text
filter_settle_samples    = 3
frontend_placeholder     = 1     /* NOT a measurement */
settle_samples           = 4
```

The end guard is `NOCTI_V4_SLOT_END_GUARD_US` = 100 µs, which is shorter than
one sample. A sample index cannot express 100 µs. The conservative grid value
is one sample (125 µs): the last sample of every slot is guard and is never
averaged. This is stricter than 100 µs by 25 µs. Class H has to show whether
the analog edge still fits; do not shrink it back to zero just because 100
does not divide 125.

Nominal stable counts when the commit lands on time, 10 Hz default
(OFF = 18):

```text
ON stable  = off_phase_samples - settle_samples
           = 18 - 4 = 14
OFF stable = slot_samples - off_phase_samples - settle_samples - guard_samples
           = 40 - 18 - 4 - 1 = 17
```

Both are at least `K_target` (10). The same inequality holds for every
allowed `fs` and both OFF phases (default and qualification). The pure
function `nocti_v4_sample_geometry_for` rejects a plan that cannot hold
`K_min` on either phase. SCAN start then fails closed.

### 2.4 Session grid

Let `S0` be the `drdy_sequence` of the first DRDY the scheduler actually
observes after `SCAN_START` has armed the sample edge. The session origin is

```text
origin = S0 + lead_samples
```

with `lead_samples` from §3. Sample `origin` is the slot-0 boundary of frame 1
(the warmup frame). There is no GPTimer modulus and no `aligned_frame_epoch`.

For frame index `f` (0-based; wire `frame_sequence = f + 1`) and slot `s`
in `0 .. 19`, all arithmetic mod 2^32:

```text
on_edge   = origin + f * frame_samples + s * slot_samples
off_edge  = on_edge + off_phase_samples
slot_end  = on_edge + slot_samples          /* exclusive */
guard     = slot_end - guard_samples
on_submit = on_edge - lead_samples
off_submit = off_edge - lead_samples
```

`nocti_v4_sample_reached(now, target)` is the signed 32-bit compare
`(int32_t)(now - target) >= 0`. It is unambiguous while the distance stays
inside 2^31 samples. 2^31 / 8000 s is about 3.1 days. A longer session must
stop and start again. Document that as a limit, do not switch to a 64-bit
index inside the ISR without an ADR amendment: the ISR already publishes a
`uint32_t`.

### 2.5 Acceptance by sequence range

A sample with sequence `q` belongs to the slot iff
`reached(q, on_edge) && !reached(q, slot_end)`.

Inside the slot the class is exactly one of:

| Order | Condition | Class | Enters K |
|---|---|---|---|
| 1 | `q >= guard` | `END_GUARD` | no |
| 2 | ON side (`q < off_edge`) and commit unknown or `q < on_first_affected` | `PRECOMMIT`, or `COMMIT_LATE` if `q >= on_edge` and the ON commit is known late | no |
| 3 | ON side and `q < on_accept` | `ON_SETTLE` | no |
| 4 | ON side otherwise | `ON_STABLE` | yes |
| 5 | OFF side and commit unknown or `q < off_first_affected` | `OFF_TRANSITION` | no |
| 6 | OFF side and `q < off_accept` | `OFF_SETTLE` | no |
| 7 | OFF side otherwise | `OFF_STABLE` | yes |

```text
on_first_affected  = on_commit_sequence + 1     /* first DRDY after the commit */
off_first_affected = off_commit_sequence + 1
on_accept  = max(on_edge + settle_samples, on_first_affected + settle_samples)
off_accept = max(off_edge + settle_samples, off_first_affected + settle_samples)
```

`on_commit_sequence` is the last DRDY index the ISR had already published when
the I2C completion ran. That conversion finished before the LED register write
completed, so it did not see the new level. The next index might have.

Rules that keep a late or early commit from being mislabeled:

- Early commit (`on_first_affected < on_edge`) does **not** pull `on_accept`
  earlier than `on_edge + settle_samples`. Samples before `on_edge` stay in
  the previous slot.
- Late commit (`on_first_affected > on_edge`) moves `on_accept` later. Samples
  in `[on_edge, on_first_affected)` are `COMMIT_LATE`, counted, and excluded.
  They are never later reclassified as stable.
- The same two rules apply to the OFF edge. An early OFF commit ends ON
  stable at `off_first_affected` instead of `off_edge`, so the last ON samples
  are not averaged as if the LED were still on.
- A missing commit leaves the phase at `PRECOMMIT` or `OFF_TRANSITION`. The
  mean is invalid. Do not fill K from the nominal window.

K for a phase is the number of `*_STABLE` samples. `K_min <= K < K_target` is
minimum-valid. `K < K_min` is under-K. Unequal ON and OFF counts still
invalidate the corrected pair, matching the current optical policy.

## 3. LED actuation

### 3.1 Who fires the edge

The DRDY ISR (`drdy_isr` in `nocti_v4_ads.c`, `IRAM_ATTR`, priority above the
LED task) already increments `drdy_sequence` and stamps the GPTimer for
metrics. After a successful ring push it calls
`nocti_v4_sample_edge_on_drdy(sequence, timestamp_us, &hpw)`.

That function, in `nocti_v4_sample_edge.h`, is `IRAM_ATTR` and does only:

1. If the sample edge is disarmed (`generation == 0`), return.
2. Publish `observed_sequence` and the split GPTimer stamp with atomics.
3. If a one-shot watch sequence matches, latch `(sequence, timestamp_us)`.
4. If no submit is armed yet, or `reached(sequence, next_submit)`, notify the
   scheduler task with `vTaskNotifyGiveFromISR`.

It does not call I2C, SPI, logging, or the LED driver. The ADS service loop
is not extended. SPI stays `spi_device_polling_transmit`. The 80 µs ADS
service budget is unchanged.

The scheduler task (priority 15, core 1) wakes, and only then builds the
existing prebuilt ON (7-byte burst) or OFF (2-byte) command and queue-sends
it to the LED task (priority 22). The LED task blocks on the I2C completion
notification. The bus transaction runs under the I2C ISR and may be sliced by
one ADS SPI, which is the PR #76 arrangement. I2C stays
`i2c_master_transmit` / `i2c_master_transmit_receive`.

Why the ISR notifies and the scheduler submits, rather than the ADS task
submitting: the ADS task is the SPI owner and must return to the next DRDY.
A queue send of a prebuilt command is short, but doing it inside the ADS
service path couples LED backlog to the 125 µs budget. The ISR notify is a
handful of atomics. The scheduler already owned the queue handoff.

Why not the LED task polling `drdy_sequence`: a poll either burns core 1 or
adds up to one wait quantum of jitter. The ISR notify is the same wake the
GPTimer alarm used to provide, now keyed off `n`.

### 3.2 Lead `k`

Runtime I2C is 800 kHz (`NOCTI_V4_I2C_SCAN_RUNTIME_HZ`). The scheduled ON
payload is the 7-byte burst (`ASYNC_TIMING_CLASS_ON_7B`). The scheduled OFF
payload is 2 bytes.

Bit budget for a 7-byte write, address included, no clock stretch:

```text
START + (addr+W+ACK) + 7 * (8+ACK) + STOP ≈ 74 bits
74 / 800000 = 92.5 µs
```

That is a lower bound. It is not a measurement. The V3 design allowance was
250 µs. ADR-0004 §3.17.3 uses a provisional `LED_COMMIT_LATENCY_BUDGET_US` of
150, and notes that `fs` 10 at 8 kSPS has almost no slack against a 175 µs
budget. Neither number has Class H evidence on this image.

`lead_samples = 2` (250 µs). The causal argument:

- DRDY of sample `E - 2` ends that conversion. The ISR then wakes the
  scheduler, which queues the LED command. I2C cannot start before that DRDY,
  so sample `E - 2` did not see the new level.
- Sample `E - 1` is the one-sample end guard of the previous slot (or the
  in-flight sample at an OFF edge). It is excluded from K.
- A commit that finishes inside 125 µs lands during the guard sample.
- A commit that finishes inside 250 µs lands on the nominal edge sample.
  Settle then blanks the next 4 samples.
- A commit that finishes later is late. `*_accept` moves later, the late
  samples are counted, and K shrinks in the open. If the nominal edge is
  already at or past `slot_end` when the scheduler wakes, the scheduler faults
  the scan and requests ALL_OFF. It does not slide the grid.

Do not raise the lead to 3. An instantaneous commit would then change the LED
during the last stable sample of the previous phase, one sample before the
guard. The guard is only one sample wide.

`lead_samples` must stay strictly less than `off_phase_samples` and strictly
less than `slot_samples - off_phase_samples`. Both are true for every allowed
plan (minimum phase length is 18 samples).

### 3.3 Late commit

On I2C completion the scheduler reads `observed_sequence` and stores it on the
slot record.

```text
late_samples = first_affected - nominal_edge     /* 0 when the commit is early or on time */
```

`sample_clock_late_commits` increments when `late_samples > 0`.
`sample_clock_early_commits` increments when `first_affected < nominal_edge`
(informational; the window is not pulled early). The optical classifier uses
the sequence rule in §2.5. There is no second path that classifies those
samples by GPTimer window "just in case."

A GPTimer submit-to-complete delta is still recorded in the existing
`led_*_latency_*_us` counters so a bench log can be compared with the old
image. That number does not move an edge.

## 4. Frame timestamp and host time

ADR-0004 §3.16.c point 5, including the formula block:

```text
anchor       = first DRDY latch at or after the slot-0 boundary
timestamp_us = that measured latch
```

It is not the nominal slot-0 start, not TX time, and not a scheduled stamp.
The current optical builder does the opposite: `nominal_frame_timestamp_us` is
`nominal_slot_start_ts - slot_index * SLOT_PERIOD_US` (`nocti_v4_optical.c`,
`builder_start`). This design replaces that value with the measured latch.

Mechanism:

- When the scheduler submits slot 0 ON, it arms a one-shot watch for
  `on_edge` of that frame.
- The DRDY ISR, on an exact sequence match, latches `(sequence, gptimer_us)`.
- Slot 0 is finalized at the slot-1 ON submit, which is
  `slot_samples - lead_samples` after the anchor (38 samples at 10 Hz). The
  latch is copied onto the slot-0 timing record before `finalized_push`.
- If the exact sample never arrives (sequence gap, overflow, stop), the anchor
  stays invalid. The frame reason gains the internal/integrity bit. Firmware
  does not substitute the GPTimer epoch.

The GPTimer value is only the capture of that DRDY. The index is the master.
Host time is `anchor_sequence / 8000` plus the session epoch the host fits
with `get_time` (ADR-0004 §3.16.d). Group delay is metadata
(`filter_settle_samples = 3`, group delay 1.5 samples) and is not subtracted.

`TS_ESTIMATED` (header flags bit 1) stays set. Q-21 Class H is **NOT RUN**.

## 5. Wire format

### 5.1 What this change does

The live control path is UDP 3333 via `tools/nn2ctl.py`. INFO and health still
say `frames=unsupported`. `nn2ctl.parse_diag` rejects `frames=ACQ1` on a
health line, and `describe_frame` requires a 1156-byte `ACQ1` payload. This
PR does **not** relabel V4 optical data as ACQ1.

The compiled V4 stream (`nocti_v4_network.c`, record type
`REC_V4_OPTICAL_FRAME` = 11) is not initialized when
`CONFIG_NOCTI_V4_NN2_COMMUNICATION` is on. It is still updated so a successor
does not extend the V3 layout by accident.

`v4_optical_frame_wire_t` bumps `version` from 1 to 2:

| Offset | Field | Version 2 meaning |
|---:|---|---|
| 0 | `version` | 2 |
| 16 | `nominal_frame_timestamp_us` | **Name kept** so the packed prefix does not move. Value is the measured slot-0 DRDY latch, little-endian, or 0 if the latch was missed. Not the nominal GPTimer slot start. |
| 1176 | `first_sample_sequence` | `uint32` LE, the slot-0 `on_edge` index |
| 1180 | `adc_fs_hz` | `uint32` LE, 8000 |

Old size 1176 becomes 1184. Header 12 + CRC 2 still fits in the 1280-byte
datagram (1198 <= 1280).

`REC_TIME64` (type 3) is stored little-endian. The previous loop wrote
`timestamp >> (56 - 8*i)`, which is big-endian and is the V3 defect ADR-0004
§3.16.f names. Version 2 readers use the frame fields above. The TIME64 record
is the same integer, LE, for a decoder that still looks for type 3.

The 12-byte stream header keeps `relative_timestamp_us` as a `uint32`. That
field is not authoritative. Replacing it changes `STREAM_HEADER_BYTES` and
every V3-compatible decoder. It stays until the NN2 envelope replaces this
datagram.

### 5.2 What remains, precisely

`frames=unsupported` is still the INFO and health string. To clear it a
successor must:

1. Bind `nocti_v4_optical_bind_completed_frame_sink` to the NN2 UDP owner on
   core 0 (the frame task is priority 5). The sink stays nonblocking. A full
   queue assigns `frame_sequence`, increments `frames_dropped`, and drops the
   frame (ADR-0004 §3.16.b).
2. Emit `msg_type = FRAME` on UDP 3333 with a schema `nn2ctl.describe_frame`
   accepts, **or** teach `nn2ctl` a versioned schema that is not the string
   `ACQ1` inside a health report. Do not put `frames=ACQ1` on the health line
   unless the payload is the 1156-byte layout `describe_frame` already parses
   (20 slots, 4 detectors, `K` 10/8, per-slot commit offsets and
   `first_adc_sequence`).
3. Set header `timestamp_us` to the measured anchor, `flags` bit 1
   `TS_ESTIMATED` set, and carry `first_sample_sequence` plus `adc_fs_hz`.
4. Only then change INFO/health from `frames=unsupported` to a new token.
   Update `tests/architecture/test_nn2ctl_diag.py` in the same commit. Keep
   rejecting a forged ACQ1 claim on a health line that is not an ACQ1 frame.

Until those four steps land, a host must not treat UDP 3333 as a frame stream.

## 6. DRDY loss, backlog, STOP/START, wrap

**DRDY loss.** The ISR stops, so no further edge is armed from the GPTimer.
The existing ADS stall watchdog (5000 µs, `NOCTI_V4_ADS_FAULT_DRDY_STALL`)
faults the session. The scheduler does not synthesize the missing indices.

**Backlog catch-up.** PR #76 policy is unchanged and lives in
`nocti_v4_backlog.h`: depth >= 8 or age >= 1 ms faults; a younger duplicate
is counted as `adc_backlog_catchup` and is not read back as a second sample.
The sample index still advances in the ISR for every DRDY that was accepted
into the event ring. LED edges follow that ISR index, not the SPI-complete
index, so a counted catch-up does not shift later slots. A dropped sample is
a hole in K, not a silent reassignment to the neighbor slot. A sequence gap
still faults (`NOCTI_V4_ADS_FAULT_SEQUENCE_GAP`).

**STOP.** `stop_internal` disarms the sample edge (`generation = 0`) before
the strong ALL_OFF. A late ISR notify observes generation 0 and does not
submit. The in-flight slot is finalized `aborted_by_stop`, as today. D6
ALL_OFF order is unchanged.

**START.** A new `generation` and a new `origin` are taken from the next DRDY
after arm. `session_epoch` increments. Old samples fail the optical epoch
check. `drdy_sequence` itself is not reset; the origin is a snapshot, so a
wrap across START still works via unsigned subtraction.

**Wrap.** `uint32_t` subtraction. Class U covers an origin at `0xFFFFFFF0`.
The signed reached-compare is valid inside a 2^31-sample window (§2.4).

## 7. RTOS, TWDT, CPU

The task table is unchanged. Agent 05 is not given a reason to move it.
Sample-edge wakes replace GPTimer alarm wakes at the same rate: two submits
per slot (400 Hz at 10 Hz optical). The first DRDY after arm also wakes the
scheduler once.

| Task | Priority | Core | Change |
|---|---:|---:|---|
| ADS | 23 | 1 | ISR calls the sample-edge hook. Service loop unchanged. |
| LED | 22 | 1 | Still the only I2C owner. |
| scheduler | 15 | 1 | Wakes on sample-edge notify instead of a GPTimer alarm. |
| optical | 9 | 0 | Classifies by sequence range. |
| dark | 6 | 0 | Unchanged. |
| frame | 5 | 0 | Unchanged. |
| net | 4 | 0 | Unchanged. |
| control | 3 | 1 | Unchanged. |

TWDT: scheduler, ADS, and dark still subscribe and kick. The scheduler idle
wait stays `NOCTI_V4_RTOS_WDT_IDLE_WAIT_MS` (1000 ms), inside the 5 s TWDT.
A stalled DRDY does not depend on the scheduler kick; the ADS task owns the
stall watchdog.

CPU: the ISR adds a few atomic stores and a rare notify. It does not add I2C
to the 125 µs period. The ADS service budget stays 80 µs
(`NOCTI_V4_RTOS_BUDGET_ADS_SERVICE_US`). No stack change. No new task.

## 8. Test plan

### 8.1 Class U (host)

File: `test/host/test_v4_sample_clock.c`, wired from `test/host/test_main.c`
and `test/host/CMakeLists.txt`. The module under test is
`firmware/v4-idf61/components/nocti_v4_common/include/nocti_v4_sample_clock.h`.
No ESP-IDF headers.

| Case | Assert |
|---|---|
| Geometry for fs 2/4/5/8/10, both OFF phases | Sample counts in §2.2. `K_target` fits. 1 Hz and 3 Hz rejected. |
| 10 Hz default edges | `on_submit = on_edge - 2`, `off_edge = on_edge + 18`, guard = last sample. |
| Settle exclusion | First 4 ON samples and first 4 OFF samples are not stable. |
| K per slot | A full slot of contiguous sequences yields ON 14 and OFF 17 at 10 Hz default. |
| Jitter and drift | Timestamps walk at +30 ppm with ±20 µs jitter. Classes follow the index. K does not change. Anchors stay strictly monotonic. |
| Late commit | `on_commit_sequence = on_edge + 1` marks one `COMMIT_LATE` and shortens ON stable. The late index is not stable. |
| Early commit | Does not pull `on_accept` before `on_edge + settle`. |
| Missing commit | No stable samples. |
| Wrap | Origin `0xFFFFFFF0`, slot still classifies. |
| u64 LE helper | Bytes are little-endian, not the old `>> (56 - 8*i)` order. |
| Reached compare | `0` does not reach `0xFFFFFFF0` backwards; `0` does reach `0xFFFFFFF0` as a forward wrap of 16. |

Python: `tests/architecture/test_v4_sample_time.py` parses a version-2 frame
tail (`first_sample_sequence`, `adc_fs_hz`) and a little-endian TIME64. It
does not accept `frames=ACQ1` on a health line.

Class H is not implied by a green host test.

### 8.2 Class H (on target) — NOT RUN

Requires a separate Owner authorization and a written procedure. Do not flash
from this PR.

1. Logic analyzer: one LED channel and DRDY. At 10 Hz default, ON and OFF
   edges fall on the planned DRDY index within the lead window. Record the
   index error, not only a microsecond delta.
2. `adc_missed = 0` and `adc_backlog_catchup = 0` over the run.
3. K histogram per slot: ON and OFF each at least 8, target 10, with the
   nominal 14/17 visible if the commit is on time.
4. Soak long enough for a 20 ppm crystal walk to exceed one sample on the old
   dual-clock schedule (several minutes). The sample-index K histogram must
   not develop edge spikes. The GPTimer-versus-DRDY period histogram is the
   drift witness, not the classifier.
5. STOP during a slot, then START. No emitter left on (D6 path). The new
   origin is a new DRDY index. The first frame remains warmup.
6. Pull DRDY (or gate it) and confirm the stall watchdog faults and the
   scheduler does not keep toggling LEDs from the GPTimer.

## 9. Phase checklist

- [x] Phase 0. This document committed and pushed before code.
- [ ] Phase 1. `nocti_v4_sample_clock.h` plus host tests.
- [ ] Phase 2. Scheduler/LED submits from the sample index. GPTimer alarms do not arm edges. Settle/guard consumed as sample counts.
- [ ] Phase 3. Optical classification by sequence range. Frame anchor from the watched DRDY.
- [ ] Phase 4. Wire version 2 and LE TIME64. `nn2ctl` parser test. `frames=unsupported` remains, for the reason in §5.2.
- [ ] Class U run recorded against the commit SHA in this checklist.
- [ ] Class H. **NOT RUN.**

## 10. Open questions

1. Class H p99 of the 7-byte PCA9956B commit at 800 kHz is unknown. `lead = 2`
   is an estimate bounded by the one-sample guard. If the measurement is
   routinely above 250 µs, K at 10 Hz default (ON stable 14) can absorb a few
   late samples before `K_min`, but the ADR-0004 fs-10 slack warning still
   applies. Do not change `lead` without updating §3.2 and the host tests.
2. The front-end placeholder is one sample. H1 may require more. Each extra
   settle sample removes one ON and one OFF stable sample. At 10 Hz default,
   ON stable is 14, so two extra samples still leave `K_target`. A third
   extra sample drops ON to 11, still above `K_min` but below `K_target`.
3. Should `drdy_sequence` become `uint64_t` before a multi-day soak? Not in
   this PR. The 2^31 reached-window is the documented limit.
4. The NN2 FRAME binder in §5.2 is the remaining protocol work. Do not close
   it by renaming `frames=unsupported` to `frames=ACQ1`.
5. Group-delay metadata is not yet a `get_status` field. Report
   `filter_settle_samples = 3` and group delay 1.5 samples when the status
   payload is next revised. Do not subtract it.

## 11. PROPOSED ADR amendment

Status of this section: **PROPOSED**. It is not an Accepted change to
ADR-0002, ADR-0003, or ADR-0004. Owner authorization recorded for the design
direction (2026-10-29 23:54 KST) is not a status flip. Do not edit the
Accepted headers of those ADRs in this PR. The D6/brownout review checks the
ADR-0003 rev 3 and ADR-0004 file rev 4 headers; leave them alone.

### 11.1 ADR-0004 §3.16.c amendment (PROPOSED)

Add after the existing anchor formula:

> On the `firmware/v4-idf61` image the slot-0 boundary is a DRDY sample index,
> not a GPTimer count. `timestamp_us` remains the measured latch of the first
> DRDY at or after that index. The host maps sample `n` with
> `t = session_epoch + n / adc_fs_hz`. `adc_fs_hz` is 8000 for the current
> ADS131M04 OSR 512 configuration and is carried on the frame. Firmware does
> not subtract sinc3 group delay (1.5 samples at this OSR). `TS_ESTIMATED`
> stays set until Q-21 Class H evidence is linked.

### 11.2 ADR-0004 §3.16.a / §3.16.f amendment (PROPOSED)

> The V4 optical datagram (`REC_V4_OPTICAL_FRAME` version 2) carries the
> anchor as a little-endian `uint64` and carries `first_sample_sequence` plus
> `adc_fs_hz`. `REC_TIME64` on that datagram is little-endian. The legacy
> 12-byte header `relative_timestamp_us` is not authoritative. The NN2 control
> socket remains `frames=unsupported` until a FRAME binder emits a schema the
> host parser accepts. That socket must not claim `ACQ1` for a V4 optical
> record.

### 11.3 ADR-0003 amendment (PROPOSED)

> No task, priority, or core change. The DRDY ISR may notify the existing
> scheduler task when a precomputed sample index is reached. The notify is not
> a new task and does not run I2C. The LED task remains the only PCA9956B
> owner. The GPTimer alarm is not the LED schedule.

### 11.4 ADR-0002 §3.6 amendment (PROPOSED)

> Settle is 4 ADC samples (3 sinc3 + 1 front-end placeholder). End guard is
> 1 ADC sample. OFF phase is the historical microsecond value rounded to the
> nearest sample. These counts are the acceptance grid. Class H may replace
> the placeholder; it may not reintroduce a microsecond window on the ESP
> crystal as the classifier.

## 12. Rollback

Revert this branch. The previous master is the GPTimer alarm in
`scheduler_alarm_handler` plus microsecond windows in `canonical_classify`.
No partition, credential, pin, or eFuse change is involved. A device that
already ran this image returns to the previous image only by an
Owner-authorized flash, which this PR does not perform.

## 13. Reference material

- ADR-0002 §3.4–§3.8 (geometry, settle starting points, 8 kSPS).
- ADR-0004 §3.16 and §3.17 N9 (measured anchor, sinc3 settle, commit rule).
- ADR-0007 (sinc3 three-period support, 375 µs versus 187.5 µs group delay;
  TI SBAS890D Rev. D §§8.3.7.1.2, 8.3.7.2, 8.4.2).
- `docs/V4_IDF61_RTOS_HANDOFF.md` and `nocti_v4_rtos.h` (task table, budgets).
- `nocti_v4_backlog.h` (depth 8 / 1 ms).
- Old feat35 `31fb2a9` mixed `esp_timer` and GPTimer. Not a build input. Do
  not copy it.
- No conflict was found between the Owner direction and ADR-0004's measured
  anchor. The conflict with the current code is the nominal slot-0 timestamp
  and the GPTimer LED alarm; that is the defect this design closes.
