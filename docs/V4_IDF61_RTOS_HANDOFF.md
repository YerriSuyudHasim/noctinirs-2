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
  |  firmware SPSC event ring of 128, sequence++, notify
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
   only the newest event that has no successor, a known age, and is younger
   than 1 ms. At 8 kSPS that 1 ms window is 8 samples, so a remainder of 8
   faults as well. The two checks are the same window. A failed or backwards
   clock is an unknown age and faults; it is not treated as a fresh sample.
   A sequence hole still faults `SEQUENCE_GAP`. Startup qualification still
   rejects a remainder of 2. CRC, status, SPI, raw-ring overflow, and STOP
   are unchanged.

Catch-up does not clear the loss. Every overwritten event increments
`adc_missed`, `adc_missed_backlog`, and `adc_backlog_catchup`, and both
`health` and `timing` print those three counters first, next to
`boot82_bound=0`. **Boot82 is fixed only when an on-target `health` report
after the measurement runs shows `adc_missed=0` and `adc_backlog_catchup=0`.**
The agreed bound is zero on both counters for each 10-second run below. One
catch-up is one DRDY the ADS task did not service inside its period, which is
the failure this change is meant to remove. A non-zero value keeps the issue
open. `event_depth_max` should stay at most 1 on that same run.

The host SPSC in `nocti_v4_backlog.h` has capacity 8. That model only checks
the index rule. The firmware DRDY ring remains 128
(`NOCTI_V4_DRDY_EVENT_RING_CAPACITY`).

`adc_service_over_budget` counts end-to-end service time, completion timestamp
minus the DRDY timestamp, against the 80 µs budget. `spi_*` remains the SPI
transaction alone. `service_*` is the end-to-end time. An unordered timestamp
pair increments `service_unknown` and the over-budget counter.

ADS, LED, and the scheduler subscribe to `esp_task_wdt` and kick at least once
a second. Their idle waits are 1 s. `CONFIG_ESP_TASK_WDT_TIMEOUT_S` is 5 and
`CONFIG_ESP_TASK_WDT_PANIC` is enabled on purpose. A missed kick means one of
those tasks is stuck. Printing the timeout and continuing would leave the
sample stream and the emitter in the state that task last set. Panic resets
through the boot path, which holds PCA RESET before I2C starts. A task that
masks interrupts for longer than the timeout can still evade the watchdog.

Subscribe and kick failures increment `task_wdt_subscribe_errors` and
`task_wdt_kick_errors`. The kick path does not log. The LED task, the
scheduler, and a health or timing request log the new total.

After subscribe, the longest ADS stretch without a kick is one 1 s watchdog
slice. `wait_for_event` kicks on entry and then blocks at most 100 ms.
`stop_dark` still allows 3 s for the raw drain, in 1 s slices with a kick
before each slice. Rate qualification is 1024 frames at 8 kSPS, about 180 ms
when every frame arrives on time, and it aborts on the first 100 ms miss.
A frame dump is at most 3 discard frames plus 4 captured frames. The channel-0
self-test is 3 discard frames plus 32 samples. With DRDY at 125 µs those two
diagnostics finish in well under 10 ms. The pathological case, every wait
returning just before the 100 ms timeout, is still one kick per wait.

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

- `adc_missed`, `adc_backlog_catchup`, `adc_missed_backlog`, `boot82_bound`
- `event_depth_max`, `adc_sequence_gap`, `crc_bad`, `spi_errors`
- `spi_min_us`, `spi_max_us`, `spi_avg_us`, `spi_ge100`
- `service_min_us`, `service_max_us`, `service_avg_us`, `service_over_budget`
- `lag_max_us`, `lag_ge250`, `sched_deferred_max_us`
- `ads_hwm_words`, `led_hwm_words`, `sched_hwm_words`

The bound that closes boot82 is `adc_missed=0` and `adc_backlog_catchup=0`,
with `event_depth_max` at most 1, on the Fs 10 run and again on the Fs 2 run.
CRC, SPI, and status stay 0, `service_over_budget` stays 0, and the ADS
high-water stays above about 128 words. Any catch-up or miss leaves boot82
open. Repeat once with `health` during the run. STOP must still end in
`STOPPED` or a sticky fault.

## References used

| SHA | What was checked | What this change does with it |
|---|---|---|
| `edb70411d2d7cc3da529c92af375bbd21f7f6016` | ADS priority 23, core 1, above LED 22 | Priority adopted |
| `df796ba6a8d0c5d6d8bc4f30fde2aa2ffea3198d` | Later feat35 checkpoint: ADS 20, scheduler 21, LED 22; nn2ctl command style | Ladder not adopted; UDP commands extended in that style |
| `b0eb800336341b4b295ffad1aaa130e15457d2dd` | CI profile selection | No runtime change taken from it |
| `d2f6d04d76773891458817d83822b2c6c23cebf9` | `feat/33-board-bringup` tip: ADS priority 5, LED 22; ADR-0005 no I2C in the ISR | Priority 5 not adopted; ISR rule kept |

Branch tip this work started from: `92fe99cf5df8b78abd6cd298553965380f2fffdb`.

Board init stages, and which of them follow feat35 `31fb2a9`, are recorded in
`docs/V4_IDF61_BOOT_INIT.md`. The task table in this file is unchanged.
