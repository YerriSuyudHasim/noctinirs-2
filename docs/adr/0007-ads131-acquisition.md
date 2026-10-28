# ADR-0007: Local ADS131M04 acquisition binding

Date: 2026-10-28. Status: implementation candidate for review.
Accountable role: Firmware Developer. Base: main PR #74 squash
`cbec2dab8e23595ae4e374e181c0d7c70f25edec`. Owner requested implementation,
build/link and host tests; no device operation, publication, merge or release.
Hardware/RTOS specialist and independent review remain required before integration.

## Scope and acceptance

Extend the existing ADS131 diagnostic and asynchronous LED owners, rather than
replace them. Optional `CONFIG_NN2_OPTICAL_ACQUISITION`, default off, combines
communication + LED + ADC. LED runtime remains800kHz, synchronous configuration
and power100kHz. SPI2, mode1,8MHz, ADS131M04 CLKIN8.192MHz, HR/OSR512/8kSPS,
gain1 and board pins remain those of the active verified board/config table.
No historical hardware configuration is imported. No analog transfer function
is established: all reported data are ADC counts, never current or I_dark.

Acceptance: CRC/status-checked DRDY records retain sequence/session and both ISR
latch and SPI completion timestamps; ambiguous/stale/multi-edge reads fail closed.
Stable-window classification must use DRDY, complete digital-filter support,
analog settling and LED completion, independent of the time the consumer reads.
Same-slot ON/OFF counts must be equal and at least8 (target10) to permit correction.
Queue pressure, fault, cancellation and restart must not reuse old dark data.
One bounded frame per20 slots goes to the existing session owner endpoint.

Owner correction in this implementation chat: **minimum optical Fs is2Hz**.
Acquisition accepts exactly{2,4,5,8,10}; the separate LED-only profile retains
its existing2..10 policy. Default5Hz/30mA, configuration immutable during scan.
This supersedes ADR-0002's1Hz allowance only for the new acquisition binding.

## Adaptation provenance and timing

Read-only references used: V3 `nocti_v3_ads131m04.c` int24/status/CRC separation;
V4 `nocti_v4_ads.c` DRDY event timestamp/session separation and sole raw consumer;
V4 `nocti_v4_scheduler.c` immutable completed-window metadata, commit-based
settling, local same-slot pairing and explicit loss on bounded-buffer pressure.
Their task ladder, unchecked historical hardware assumptions and entire runtimes
are not imported. Existing NN2 ADS register readback/CRC, owner mailbox,
poisoned-transaction lifetime, watchdog and LED deadline/cancellation mechanisms
remain in use. No reference file is a build input.

TI SBAS890D Rev.D, sections8.3.7.1.2/8.3.7.2/8.4.2, describes the sinc3 filter and
three-data-period support (Equation6), plus two initial fast-start samples.
[TI datasheet](https://www.ti.com/lit/ds/symlink/ads131m04.pdf).
Continuous samples after an optical change require the full375us filter support,
not merely the187.5us group delay or a fresh SPI read. After successful PCA phase
work, publish a value window in the common esp_timer domain:

- lower acceptance boundary = software completion +500us analog settle +375us;
- upper exclusive boundary = earliest next nominal phase alarm minus100us guard;
-500us/100us are inherited provisional ADR-0002 starting values, not measured
  analog settling or jitter guarantees;
- a delayed phase completion shrinks the window; missing/unequal/under8 counts
  invalidate the pair, with raw phase sums/counts still retained;
- first complete frame is WARMUP and is never valid corrected data.

LED prepare starts GPTimer after latching esp_timer. This latch is an earliest
nominal timer origin: any delay before timer start makes the upper limit more
conservative. Phase completion is latched after the owner returns, including
phase39 health work. No physical optical edge is inferred. DRDY is a software ISR
latch; records and FRAMEs explicitly use TS_ESTIMATED. ISR lateness/cache stalls
cannot be calibrated or bounded by host tests; Class H must establish timestamp
error and guard adequacy. The adapter rejects service age>=125us, an unexpected
DRDY serial, or another edge before SPI completion; it never presents FIFO catch-up
as contiguous. The original gap/coalescence/CRC/overflow counters remain.

## Ownership, memory and recovery

ADC task still exclusively owns SPI/reset/registers. LED task still exclusively
owns I2C/GPTimer. New pairing task(core1/priority4/6144-byte stack) creates and
serializes the ADC port; priorities ADC5 and LED22 remain unchanged. A64-value
window queue connects LED to pairing, nonblocking. A4-frame queue connects pairing
to the existing main/network owner, also nonblocking. No producer uses sockets.
All policy/queues/stacks use internal memory; PSRAM/octal guard remains mandatory.
The expanded raw record is bounded56bytes; adapter static-state bound stays12KiB (target map:11968bytes).
The128-record ADC queue covers16ms nominally, still requiring the original10ms
consumer budget. RPC draining/LED preemption coexistence is **unqualified**.

Cancellation is serialized with permission plus nonblocking LED start (lock order
pairing gate -> LED). SAFE_OFF immediately cancels LED, then pairing requests ADC
STOP. IDLE is exposed only after STOP acknowledgment and LED cleanup. Timeout
never proves stop. An unacknowledged stop blocks restart; poisoned SPI requires
reboot. Window queue overflow, ADC loss or LED fault cancels the session. Explicit
clear/start is required after fault; initialization flushes/counts old raw records.
START resets pairing/windows/frames. Completed queued frames discarded by STOP
count as drops; the unfinished tail increments `acq_tail`. No cross-session pair.
The tail is deliberately withheld; frames finalize when chronological samples
enter the next frame. Thus STOP can discard the last complete-looking tail.

## ACQ1 local wire extension

The existing NNS2 ADR0004/1 envelope/CRC32, peer lock, lease and command registry
are preserved. This is an explicit local FRAME payload version (`ACQ1`), **not**
a claim to implement the upstream Proposed556-byte single-detector format or its
unresolved four-detector Q-11. Real acquisition replies clear SIMULATED; every
FRAME sets TS_ESTIMATED. LED-only/default replies keep their existing provenance.

All integers LE; FRAME payload1156bytes, datagram1188bytes:

| Offset | Value |
|---|---|
|0|ASCII ACQ1|
|4,8|u32 optical frame sequence, ADC session|
|12|u64 earliest nominal slot0 start in esp_timer microseconds|
|20..23|u8 slots20, detectors4, K_target10, K_min8|
|24,26,28|u16 analog settle500, filter support375, guard100 in us|
|30,31|u8 warmup, reserved0|
|32|u32 producer frame drops in this START|
|36|20 records of56bytes|

Each slot: u8 slot/n_on/n_off/quality(0 TARGET,1 MINIMUM,2 UNDER_K,3 INVALID,
4 WARMUP); u32 invalid-pair reason(bit0); eight i32 raw ON[4]/OFF[4] **sums**;
u16 ON/OFF commit offsets from nominal **slot** start; u16 ON/OFF stable lengths;
u32 first selected ON/OFF ADC sequence. No selected sample is converted to current.
At most10 signed int24 values safely fit each i32 sum. INT32_MIN means no samples;
a short/warmup phase retains its sum/count but must not be used scientifically.
CH0..3 remain in ADC order (PD3,PD4,PD1,PD2); logical slots use the active PCA map.

`nn2ctl.describe_frame` preserves exact raw sums/counts, derives phase means, and
computes `corrected_counts = on_sum/n_on - off_sum/n_off` only for valid pairs.
No integer-mean truncation is introduced. `nn2ctl run --led --acquisition` prints
FRAME JSON during heartbeat consumption (advisory adds existing
`--ack-short-warning`); these are tool capabilities, not hardware authorization.
Producer overflow drops newest/counts; network send is nonblocking/drop/count.
Optical frame sequence, envelope sequence and ADC sequence remain separate.
USB cached ADC fault/loss/overflow/CRC/missed/unknown/drop/stop-ack telemetry
is refreshed by the pairing owner per completed frame and during stop.

## Verification and next board step

See `docs/ADS131_ACQUISITION_RESULTS.md` for exact source and commands.
Rollback: default profile or reviewed revert of this candidate; no flash/NVS/
partition/security changes are performed here. All Class H, stack watermark,
ISR latency, analog settling, ADC/LED coexistence, SI and optical evidence are
NOT RUN. Next authorized bench procedure must check CLKIN/DRDY period, register
readback, reset/SYNC/FIFO, signed known inputs, actual commit/DRDY alignment,
K at Fs10, traffic pressure, STOP during transfer, fault cleanup and restart.
Prior LED-only physical success supplies no ADC integration evidence.
