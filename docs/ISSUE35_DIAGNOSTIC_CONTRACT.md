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
MODE=0110 (reset indication cleared, timeout enabled, level DRDY);
CLOCK=0F0A; GAIN1=0000. Writes have individual pipelined WREG acknowledgments
and masked readback; ID mask FF00 expects2400. Reserved ID low bits are ignored.
Six 24-bit words are always transferred; response/register/CRC words are MSB
aligned with zero low-byte padding. Configuration exchanges check CRC too.

Reset holds LOW for 251us (greater than2048 clocks), then waits5us before SPI.
Init verifies first DRDY within20ms. Start strobes SYNC LOW1us (8 clocks,
less than2048), clearing device FIFO after the configuration pause; clears stale
notifications and discards the first two fast-start samples. No catch-up reads
are labelled contiguous. Coalesced notifications or a service gap over250us
fault/reset the adapter with loss lower bounds and an unknown-loss indication.
That conservative choice needs bench feasibility measurement at8kSPS.

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
Read waits for data in bounded tick steps; EMPTY means no record by its deadline; IO means device/protocol/integrity failure; TIMEOUT
means the command deadline expired, not proof of quiescence. Start requires READY;
init requires STOPPED/FAULT, not RUNNING; stop is idempotent and holds ADC reset.
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
Tick rounding is ceil(ms*tick_hz/1000), minimum1: at100Hz one tick is10ms,
not a250us wall-clock detector. A30ms stall budget is conditional on scheduling;
hard time bounds, interrupt latency and SPI completion remain unmeasured.

The consumer may block one tick on EMPTY (10ms at100Hz); the
10ms tick interval is the proposed stall budget and is counted as overflow rather than concealed. Budget and
consumer scheduling require RTOS assessment before any throughput claim.
Task provisional priority5/core1/stack4096 bytes; no historical priority ladder
adopted, no integrated scheduler exists here. Static internal memory only, no
PSRAM buffers/stacks. TWDT subscribes while the owner task lives and resets once
per bounded loop; idle/fault loops block. Failure to subscribe/reset faults the
candidate. ISR is flash-resident, service flags0: no cache-off support or exact
miss count. Diagnostic excludes flash writes/OTA; unexpected cache stalls may
cause detected service-gap fault or unknown loss, never timing certification.

Board Nocti v1/N16R8: RESET6 CS7 DRDY35 SCLK36 MISO37 MOSI38. Only board table
contains pin values. No GPIO26 configuration/readback, eFuse or emitter IO18 code.
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
