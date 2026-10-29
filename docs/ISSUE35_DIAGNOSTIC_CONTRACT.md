> Current local candidate: see [FEAT35_V4_BASELINE](FEAT35_V4_BASELINE.md).
> It supersedes the earlier timing-guard/queue/LED-start details below. The
> ADC-only dark baseline has a different host test command. No build performed.

# Issue 35 ADC diagnostic candidate contract

Status: Proposed; author implementation, awaiting scoped Architect, RTOS,
Hardware and independent Reviewer/Verifier assessment. This is a temporary
compatibility step toward ADR-0003's intended common ADC port. The scalar demo
port is preserved. `nn2_port_adc_stream_t` is not an approved permanent second
ADC architecture. Scope record: issue comment 6320131678. No optical runtime,
emitter control or physical default-off claim is included.

## Reference and protocol

TI [SBAS890D Rev. D, revised May 2021](https://www.ti.com/lit/ds/symlink/ads131m04.pdf),
downloaded 2026-10-21 UTC: 3,605,075 bytes, SHA256
`cc5bfca62f8b1b0dbd956d786943616cd28414ccce6c15cc9e503e669cb89d71`.
Repository historical PDFs remain unchanged. Relevant printed pages: 9-10
(mode-1 timing/reset), 20-21 (clock/OSR), 28 (CRC), 29-30 (reset/startup),
35-39 (DRDY, word alignment, two-deep device FIFO), 41-43 (single-register
pipeline), 47-55 (ID/STATUS/MODE/CLOCK/GAIN1).

The diagnostic tuple is deliberately restricted: 8.192 MHz CLKIN, HR, OSR512,
8 kSPS, all four channels gain1, 24-bit words, CCITT seed FFFF/poly1021,
non-reflected/xorout0; input/register CRC disabled, output CRC always checked.
MODE=0111 (reset indication cleared, timeout enabled, pulse DRDY);
CLOCK=0F0A; GAIN1=0000; CFG=0600; CH0/1/2/3_CFG=0000. Writes have individual pipelined WREG acknowledgments
and masked readback; ID mask FF00 expects2400. Reserved ID low bits are ignored.
Six 24-bit words are always transferred; response/register/CRC words are MSB
aligned with zero low-byte padding. Configuration exchanges check CRC too.

Reset holds LOW for 300us, then waits20us before a CRC-checked NULL/reset
acknowledgment (FF24). UNLOCK precedes configuration, following V4. Every
configuration write retains explicit WREG acknowledgment and masked readback.
Init verifies a DRDY interrupt within20ms; tick-spaced level polling is not used
for pulse-mode readiness. Start disables the pin interrupt, clears recorded
events, strobes SYNC LOW5us, then enables capture/interrupts. Three post-SYNC
frames are CRC/status checked and discarded, followed by 1024 qualification reads
before START acknowledgment. Qualification accepts7600..8400 SPS and rejects any
observed interval over188us; all qualifying frames are discarded. The acquisition
caller allows500ms START, while the owner caps qualification at400ms within that
same caller deadline. START success does not establish physical sample continuity. One
F_RESYNC is permitted only in those discarded frames, following V4; a repeated
or runtime F_RESYNC faults acquisition. Runtime requires all four channels fresh.

Recorded events are consumed individually. Two pending events are allowed;
dropped events, a third pending event, discontinuous serials, oldest-event age
reaching250us or non-settling service gap over250us fault/reset the adapter.
Transport completion and serial are captured before CRC/decoding. CRC is checked
once per runtime read. Legacy count-only seams retain single-event/125us guards.
These additional age guards are more conservative than V4; no timing/physical
FIFO-association guarantee is established without board validation.

## Values and port operations

One record contains four simultaneous signed int24 counts sign-extended to int32
in CH0..CH3 order. PD1/2/3/4 map to CH2/3/0/1. Sequence counts observed DRDY
notifications, modulo2^32, across sessions. Session increments on successful init.
`read_complete_us` is software monotonic microseconds after transfer; it is not
a conversion edge or an optical classification timestamp. Source is ADC.
Quality includes discontinuity/overflow; CRC-bad records are never delivered.
No physical measurement exists merely because a source label says ADC.

All stream functions: one composition/Application task caller, ISR forbidden,
nonreentrant; caller serializes the handle. Adapter context/static resources live
until reboot. Inputs are copied for the call; no borrowed pointers are queued.
Read copies one record; counters copies a coherent owner snapshot. Outputs are
unchanged on any non-OK result. Null/zero or timeout>1000ms is INVALID_ARG.
Read waits for data in bounded tick steps. EMPTY means timely EMPTY replies
exhausted the outward read budget between retries; TIMEOUT means enqueue or reply
acceptance reached that same absolute deadline. A late EMPTY reply is TIMEOUT too.
IO means device/protocol/integrity failure. At `now >= deadline`, no matching
reply is accepted and no output is copied. Rounded RTOS waits never renew the
budget, including nested read retries. A timeout is not cancellation, completed
STOP, proof of quiescence, or a guarantee of physical return latency. Start requires READY;
init requires STOPPED/READY/FAULT, not RUNNING; stop is idempotent and holds ADC reset.
Reinit flushes buffered records and counts them as lost; lifetime counters persist.
Application always requests stop after attempted initialization or acquisition;
it never automatically restarts on an acquisition fault.

## Ownership and bounded lifecycle

Only `nn2_ads131` task drives ADC reset/register/SPI and changes acquisition state.
The ISR only notifies. Composition creates the task with a copied board table.
A single-slot value request/reply mailbox plus monotonically identified requests
serializes port calls. Expired commands are rejected by the owner; late replies
are discarded by the caller. Deadline expiration forbids assuming success.
Command enqueue/ack use one total deadline; stop wakes the task with the high bit of the
notification word; lower31 bits count DRDY (eIncrement). Control never aborts SPI
queue waits. The bounded notification wait also expires every RTOS tick. Commands are processed before any notified read.
No caller stack pointer crosses the mailbox. No shared Core ring is used.
Rejected/expired lifecycle commands that leave RUNNING retain captured DRDY and
combine newly pending notifications before service. Coalescence faults/counts the
discontinuity rather than reading stale catch-up frames. START drains pre-sync
notifications only when READY; successful sync and sync failure into FAULT both
invalidate pre-sync events. STOP/reset success or failure into FAULT ends live
acquisition. These effects, not opcode or OK alone, decide resynchronization.
A post-dispatch notification remains pending if it arrives after the service
snapshot. This control glue is an adapter-private include shared by the ESP task
and deterministic host RTOS mocks; it adds no public ABI or inner SDK dependency.

The adapter owner exclusively accesses a128-record overwrite-oldest queue. Each
overwrite increments overflow and lost, and sets persistent discontinuity quality
on subsequent records. Counters are uint32 modulo2^32 (including sequence); each
snapshot is coherent because the owner copies it. Produced counts valid parsed
records; consumed counts port deliveries; lost counts known queue/CRC/read losses;
adc_missed is only an observed lower bound. Unknown/cache-off losses are explicit.
No physical total-conversion count is claimed. Stop retains queued records;
init flushes/counts them. Fault preserves records and counters for inspection.

Proposed diagnostic consumer budget10ms at8kSPS:80 frames plus25%=100; round to128
for16ms capacity. This explicitly replaces the old50ms assumption for this bounded
profile. It does not meet50ms: that would need500 records. `sizeof` and map evidence
must accompany review; record size <=48B gives <=6144B; the complete static adapter state including
4096B stack/mailboxes is compile-asserted <=12288B. Init driver allocation is
additional and retained in map/heap review.
Overflow remains observable when consumer scheduling exceeds the proposed budget.

## ESP implementation requirements and limits

One dedicated SPI2 bus, DMA disabled, mode1 <=8MHz, queue depth1. Bus-already-used
is an error, never silently accepted. SPI queue and result waits use finite ticks;
transaction/buffers are static. A result timeout poisons the bus until reboot:
do not reuse/free an in-flight transaction or claim that reset cancels the driver.
Inspect the pinned driver's bounded queue/result behavior in the review evidence.
SPI wait rounding is ceil(ms*tick_hz/1000); control waits use ceil(remaining_us*tick_hz/1000000), minimum1: at100Hz one tick is10ms,
not a250us wall-clock detector. A30ms stall budget is conditional on scheduling;
hard time bounds, interrupt latency and SPI completion remain unmeasured.

The consumer may block one tick on EMPTY (10ms at100Hz); the
10ms tick interval is the proposed stall budget and is counted as overflow rather than concealed. Budget and
consumer scheduling require RTOS assessment before any throughput claim.
The SYNC critical-section requirement is <=10us including GPIO overhead, unmeasured.
Task provisional priority5/core1/stack4096 bytes; no historical priority ladder
adopted, no integrated scheduler exists here. Static internal memory only, no
PSRAM buffers/stacks. TWDT subscribes while the owner task lives and resets once
per bounded loop; idle/fault loops block. Failure to subscribe/reset faults the
candidate. ISR is flash-resident, service flags0: no cache-off support or exact
miss count. Diagnostic excludes flash writes/OTA; unexpected cache stalls may
cause detected service-gap fault or unknown loss, never timing certification.

Board Nocti v1/N16R8: RESET6 CS7 DRDY35 SCLK36 MISO37 MOSI38. Only board table
contains pin values. Creation accepts only the address of that immutable table,
then copies it; arbitrary pin structures are rejected before GPIO operations. No GPIO26 configuration/readback, eFuse or emitter IO18 code.
The fake/disabled profile stays default; diagnostic is mutually exclusive opt-in.
Generated configuration must reject PSRAM and octal flash, select DIO/16MB and USB
Serial/JTAG console for diagnostic. This software choice does not disconnect bonded
PSRAM, resolve cable contacts/back-feed, establish physical emitter-off, or waive#19.

## Verification, recovery and handoff

Production protocol/lifecycle logic is exercised through fake SPI/reset/clock/DRDY
seams with pinned Unity; Application use case through a fake stream port. Native
and target checks retain commands/dependencies/nonzero and negative controls.
No new inner-layer A2 waiver is inferred: changed headers/use-case need scoped
Architect/Verifier transitive-chain assessment. Review actual target graph.
Recover software fault by stop then explicit reinit; poisoned SPI needs reboot.
Rollback is a reviewed revert or default fake profile; no device installation here.

All hardware, timing, jitter, stack watermark, electrical/optical/safety/clinical
evidence is NOT RUN. Required next step is one batched changed-candidate specialist
and independent review arranged by Orchestrator; this author supplies no approval.

## Local timing-history / IRAM diagnostic candidate

The last eight attempted reads retain serial, DRDY timestamp/period, transport
begin/end, CRC duration, pending depth, status and completion phase. The first
fault freezes history; ads131_trace and ads131_rate are emitted after reset,
never once per successful sample. Successful qualification is logged only after
STOP, so console I/O cannot create a gap at the START-to-LED handoff.

GPIO uses an IRAM handler with scalar event-ring writes, internal DRAM state,
IRAM timer access and FreeRTOS ISR notification. Both ADC profiles explicitly
require timer/FreeRTOS/SPI master IRAM placement. An already-installed GPIO ISR
service is rejected because its allocation flags cannot be verified. The user
must regenerate/build the selected profile; source audit does not replace map
verification. No full cache-off or scheduling guarantee is established.

The retained250us service-gap/age guards are intentionally not removed in this
candidate. Boot53 may still fault during qualification; its preceding history
is now available to distinguish inter-DRDY gaps from slow SPI/CPU handling.
