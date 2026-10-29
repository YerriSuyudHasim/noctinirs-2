# V4 IDF 6.1 real-time handoff

Static design and host-test record for the boot82 `EVENT_BACKLOG`. The image
was compiled with ESP-IDF 6.1 for ESP32-S3 and was not flashed or measured on
a board. A green host test or a successful link is not timing, electrical, or
clinical evidence.

Author link, this workspace, not a released image: app binary about 0xdb530
bytes, DIRAM 276211/341760 bytes (80.82%), and the 16384-byte IRAM window full.
No further `IRAM_ATTR` was added on top of the existing SPI, timer, GPIO, and
I2C cache-safe options.

## Task table

Budgets are runnable CPU windows. I2C bus time may be longer because the LED
task blocks on the completion notification and the ADS task may preempt it for
one SPI. Stacks are bytes. High-water marks on the wire are FreeRTOS words.

| Task | Responsibility | Core | Priority | Stack | Budget |
|---|---|---|---|---|---|
| `nocti_v4_ads` | DRDY pop, one SPI, CRC, raw SPSC push. No I2C, no printf. | 1 | 23 | 4096 | service ≤ 80 µs inside the 125 µs period |
| `nocti_v4_led` | Queue one prebuilt PCA payload and block for the I2C ISR. No schedule policy. | 1 | 22 | 4096 | submit CPU ≤ 40 µs, then block |
| `nocti_v4_sched` | Absolute slot edge and nonblocking LED handoff. Slot state stays here. | 1 | 15 | 6144 | handoff ≤ 50 µs, then block |
| `nocti_v4_optical` | Raw-batch classification. Not on the DRDY core. | 0 | 9 | 6144 | soft; must not enter core 1 |
| `nocti_v4_frame` | Completed-frame consumer. | 0 | 5 | 4096 | soft |
| `v4_nn2_net` | UDP owner: commands, health, timing. No SPI. | 0 | 4 | 16384 | on demand |
| `v4_control` | Stopped-only I2C setup and STOP cleanup. Below the ladder. | 1 | 3 | 8192 | not in the 8 kSPS window |
| `nocti_v4_dark` | Dark-only raw drain. | 0 | 2 | 4096 | idle during scan |

The IDF Wi-Fi task stays on core 0 at its SDK priority, above this soft band.
PSRAM stays off: IO35–37 belong to the ADS SPI bus.

## Handoff

```text
DRDY ISR (IRAM, core of the GPIO service)
  |  SPSC event ring, sequence++, notify
  v
nocti_v4_ads prio 23
  |  one SPI + CRC, raw SPSC + sequence
  v
nocti_v4_optical prio 9 (core 0)

GPTimer ISR
  |  deadline ring + sequence
  v
nocti_v4_sched prio 15
  |  nonblocking command queue
  v
nocti_v4_led prio 22
  |  I2C ISR completion ring
  v
nocti_v4_sched drains the completion before the next edge

v4_nn2_net (core 0) reads atomics only when the host asks
  health / timing  -- not an ACQ1 frame
```

ISR work is the existing IRAM DRDY and GPTimer handlers: timestamp, ring slot,
sequence, notify. `CONFIG_SPI_MASTER_IN_IRAM`, `CONFIG_ESP_TIMER_IN_IRAM`,
`CONFIG_GPIO_CTRL_FUNC_IN_IRAM`, and `CONFIG_I2C_ISR_IRAM_SAFE` stay set. The
ADS service body remains in flash; the boot82 failure was preemption, not a
cache miss of a 33 µs SPI. Lag bins on `timing` are the measurement of whatever
jitter remains.

## Boot82 evidence against the three hypotheses

Reported, and not re-measured here: baseline `V4_IDF61_NN2_CTRL`, boot82, rate
qualification 1024 samples at 8000 SPS with periods 124–125 µs, then the first
scheduled LED ON completed, then `EVENT_BACKLOG` at `PROCESS_EVENT`. SPI
32–45 µs (average 33). CRC, SPI, and status errors 0. DRDY depth max 4,
`adc_missed_backlog` 3, raw-ring overflow 0. Scan report about 3085 µs and no
completed frame. PSRAM is off in the migration sdkconfig because those pins are
the ADS bus. That does not make the backlog a memory-capacity fault.

1. **LED priority 22 starved ADS priority 20.** Supported by the source, not by
   a trace. Both tasks were pinned to core 1. `submit_payload` calls blocking
   `i2c_master_transmit` and then waits for the callback, all inside the
   priority-22 task. Four queued DRDY events are about 500 µs, which a first
   800 kHz transaction can cover if the LED task does not block promptly or if
   it resumes above ADS afterwards. The SPI counters show the transport itself
   fits in the 125 µs period once the ADS task actually runs. This image puts
   ADS at 23, the order from `edb7041`, so a DRDY notify preempts that LED
   task. One SPI is inside the 500 µs LED settle budget.

2. **Scheduler priority 21 ran after the ON handoff, above ADS.** Supported.
   `process_deadline` returns from the zero-wait queue send and then finalizes
   the previous slot, updates lateness, and accounts deferred work before it
   blocks. That code was priority 21. The completion path notifies the same
   task again. Both windows outranked ADS. The scheduler is now priority 15.
   The bookkeeping is unchanged and still ordered before the next edge; it can
   no longer run instead of a pending DRDY service.

3. **`MAX_EVENT_BACKLOG` 2 hard-faulted with no catch-up.** Confirmed in the
   previous `process_event`. After the current event was popped, a remainder
   of 2 or more counted `adc_missed_backlog` and faulted without a read.
   Boot82's remainder was 3, matching depth 4. ADS131M04 does not keep a
   multi-sample FIFO, so those older events must not be read as distinct
   samples. The running rule now drops each overwritten event, counts it in
   `adc_missed`, `adc_missed_backlog`, and `adc_backlog_catchup`, and reads
   only the newest event that has no successor and is younger than 1 ms.
   A remainder of 16, or a newest event already 1 ms old, still faults
   `EVENT_BACKLOG` and counts every pending event. A sequence hole still
   faults `SEQUENCE_GAP`. Startup qualification still rejects a remainder of
   2. CRC, status, SPI, raw-ring overflow, and STOP are unchanged.

`df796ba` put this project's ADS priority back to 20 under LED 22 and
scheduler 21. That is the same starvation order boot82 hit. It was not copied.
`b0eb800` aligns CI profiles and does not change the ladder. `feat/33` at
`d2f6d04` still has ADS priority 5 under LED 22; ADR-0008 already records that
order as the earlier GAP failure. ADR-0005's "no I2C or logging in the ISR"
rule is kept. The USB command task, which could printf on core 1, is gone.

## What the wire does not claim

`INFO` says `frames=unsupported`. `health` repeats `frames=unsupported`.
`nn2ctl` rejects a health report that says `frames=ACQ1`. No V4 optical
record is mapped onto the ACQ1 field layout.

`timing` reports `cpu=not_measured`. A runtime-stats timer was not added.
Stack high-water marks are sampled on the UDP task when the host asks.

## On-target plan

Authorized build from `firmware/v4-idf61` only. Do not use a root-tree image.
Then, from the repository root, against `192.168.4.1:3333`:

```powershell
python tools/nn2ctl.py info
python tools/nn2ctl.py health
python tools/nn2ctl.py timing
python tools/nn2ctl.py run --rate 10 --seconds 10 --led
python tools/nn2ctl.py health
python tools/nn2ctl.py timing
```

Record the image hash, IDF version, and these fields together:

- `event_depth_max`, `adc_missed`, `adc_missed_backlog`, `adc_backlog_catchup`
- `adc_sequence_gap`, `crc_bad`, `spi_errors`, `drdy_event_overflow`
- `spi_min_us`, `spi_max_us`, `spi_avg_us`, `spi_ge100`, `service_over_budget`
- `lag_max_us`, `lag_ge250`, `sched_deferred_max_us`
- `ads_hwm_words`, `led_hwm_words`, `sched_hwm_words`

A useful first run keeps depth max at 1, catch-up at 0, CRC/SPI/status at 0,
`spi_ge100` at 0, and `lag_ge250` at 0, with ADS high-water still above about
128 words. Any other result is evidence, not a pass. Repeat at Fs 2, where a
slot is 25 ms and the raw stage is the limiting buffer, and once with the
host sending `health` during the run. STOP must still end in `STOPPED` or a
sticky fault, never a fabricated stopped success.

## References used

| SHA | What was checked | What this change does with it |
|---|---|---|
| `edb70411d2d7cc3da529c92af375bbd21f7f6016` | ADS priority 23, core 1, above LED 22 | Priority adopted |
| `df796ba6a8d0c5d6d8bc4f30fde2aa2ffea3198d` | Later feat35 checkpoint: ADS 20, scheduler 21, LED 22; nn2ctl command style | Ladder not adopted; UDP commands extended in that style |
| `b0eb800336341b4b295ffad1aaa130e15457d2dd` | CI profile selection | No runtime change taken from it |
| `d2f6d04d76773891458817d83822b2c6c23cebf9` | `feat/33-board-bringup` tip: ADS priority 5, LED 22; ADR-0005 no I2C in the ISR | Priority 5 not adopted; ISR rule kept |

Branch tip this work started from: `92fe99cf5df8b78abd6cd298553965380f2fffdb`.
