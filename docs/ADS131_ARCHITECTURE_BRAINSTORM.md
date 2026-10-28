# V3 / V4 / NoctiNIRS 2 feat35 architecture brainstorming notes

Date: 2026-10-28. Evidence: static source inspection only.
Status: discussion notes, not a revision request, accepted ADR or implementation.
The original study made no firmware/host/configuration/reference edits, commits,
pushes or hardware actions. These notes are now published for follow-up; their
proposals remain unapproved and unimplemented. Build recovery is recorded separately
in `FEAT35_BUILD_RECOVERY.md`.
Active candidate: feat/35-ads131-acquisition at31fb2a9101fbb12a59e52506464d476e184bbdd9.
Reference integrity:319 files verified before and after reading.

## Scope and source identities

- V3 actual version header:3.4.6 / P5_ADS_SYNC_STATUS_FIX. Some architecture docs
  still say3.4.0; actual source takes precedence for this comparison.
- V4 actual version:4.0.0-v4-7-2-rt-handoff / V4-P7.2-WIFI-RT-HANDOFF-REPAIR.
- V4 patches/v4-7-2-idf-5-5-5 is explicitly an unbuilt patch candidate;
  its README says acquisition/packet semantics are unchanged. It is not treated
  as a newer hardware-verified runtime.
- Historical comments mentioning hardware verification are historical records;
  no such measurements were reproduced in this task.

## System architecture and ownership

V3: DRDY ISR -> event SPSC -> ADC task -> raw SPSC -> optical processing
-> completed-frame SPSC -> Wi-Fi stream task -> encoder -> UDP.
Scheduler and frame processing share the optical component, but run as distinct
owners/tasks. LED control and fault recovery have dedicated tasks.
Core1 priorities:ADC20 > scheduler19 > LED18 > recovery10.
Core0:optical12, command9, UDP8, health5.

V4: DRDY ISR -> event SPSC -> ADS owner -> raw SPSC -> optical batch consumer;
GPTimer -> scheduler -> async LED owner -> immutable finalized slot timings
-> optical classification -> completed-frame consumer -> network frame ring -> UDP.
Core1:LED22 > scheduler21 > ADC20. Core0:optical9 > frame5 > network4.
Health counters, anomaly history, frame/slot identity and drain barriers are richer.
Acquisition start/stop is serial controlled; UDP stream control is independent.

NN2 feat35: ISR count + latest coherent DRDY latch -> ADC owner ->128-record queue
-> per-record request/reply ADC port -> pairing worker + LED windows ->4-frame queue
-> Application NNS2 envelope/session policy -> main/network polling owner -> UDP.
Core1:LED22 > ADC5 > pairing4. Core policy is pure C11; RTOS/SPI/I2C/sockets
remain outside Core. UDP controls acquisition as well as transport.

Observation: task numbers alone do not prove timing. Relative ordering, blocking
operations, interrupt affinity and measured service delay matter. V4's ADC20 and
NN2's ADC5 have different scheduling environments; do not copy numbers blindly.

## ADS configuration and acquisition

All three target the restricted8.192MHz / OSR512 /8kSPS / four signed int24 counts
configuration in their respective source. Historical SPI3 pins/bus assumptions
must not replace NN2's active SPI2 board table. V4 explicitly selects DRDY pulse;
NN2's existing diagnostic tuple uses level DRDY. Neither format should be changed
just to match another generation without a reviewed board/timing reason.

V3/V4 SPI transport uses polling transmit in the ADC owner task. NN2 uses finite
queued SPI submission/result waits, retained static transaction storage and a
poisoned-bus policy after unresolved completion. These are different latency and
recovery contracts, despite identical18-byte frame geometry.

V3 and V4 measure startup conversion rate and inspect continuity/CRC/status.
V4 validates1024 observations with a5% SPS tolerance and records min/max/average
period. It verifies channel configuration plus OCAL/GCAL defaults and offers
frame dumps, channel-register dumps and CH0 self-test. Its source records an old
register-stride bug that could set zero channel calibration gain: CRC correctness
alone would not expose a logically wrong but successfully transmitted setting.

NN2 verifies ID and MODE/CLOCK/GAIN1 readback, waits for initial ready, then SYNCs
and discards two frames before delivery. It does not currently implement V4's
measured-SPS gate, full channel calibration snapshot or equivalent frame-dump API.
V3/V4 post-SYNC policy discards three frames and explicitly budgets expected
F_RESYNC. NN2 does not separately classify expected/unexpected F_RESYNC and skips
normal STATUS parsing during its two discarded frames (CRC still checked).
These policies are not interchangeable with optical-edge filter settling.

## Buffers, timing and representative values

| Item | V3 | V4 | feat35 |
|---|---|---|---|
| DRDY event buffering |128 immutable events|128 immutable events|Notification count + latest timestamp/serial|
| Raw ADC capacity |1024 records, nominal128ms|256 records, nominal32ms|128 records, nominal16ms|
| Raw overflow |No overwrite; fault|No overwrite; fault|Overwrite oldest + sticky loss quality; consumer subsequently faults|
| Optical processing |Stage raw samples and finalized timing|Batch32; stage raw/finalized timing, chronological reconciliation|Consume one record per RPC; scan retained phase windows|
| K choice |Newest contiguous valid tail, up to10|Newest tail, up to10|First10 accepted samples|
| ON/OFF counts |Both independently >=8 may be minimum-valid|Both independently meet minimum; no equality test in finalize_work|Requires equal counts >=8|
| Detail |Timing reasons, sample-integrity flags|Available vs used counts; rejection categories; anomaly/phase diagnostics|Used counts, sums, commit/stable metadata; broad invalid reason|

An event ring stores timestamps, not unlimited ADC conversion history. V3/V4
still bound event backlog according to their device FIFO integrity contract.
Increasing event capacity does not make arbitrary catch-up SPI reads valid.
NN2 deliberately faults on coalesced notifications or service/read ambiguity;
this tighter service policy must be qualified under LED/Wi-Fi load.

V3/V4 classify by DRDY and actual commit-derived ON/OFF boundaries. V4 waits for
finalized complete timing and staged raw data to reach the slot boundary, then
reconciles mutually exclusive sample categories. NN2 also uses DRDY rather than
SPI completion, with explicit additional375us full-filter support after500us
analog settling and a100us nominal deadline guard. These are provisional software
windows; no new board evidence establishes analog settling or ISR timestamp error.
V3/V4 source uses settle windows but not NN2's separately named375us optical-edge
support term. This does not prove one physical timing policy is correct.

V3/V4 calculate integer means/corrected values on device. NN2 transmits exact
bounded raw phase sums/counts; host computes sum_ON/n_ON - sum_OFF/n_OFF and
avoids truncating each mean first. Counts are not amperes, calibrated intensity,
HbO/HbR or proof of analog transfer-function accuracy.

## Error detection and handling

V3 distinguishes isolated SPI/CRC/STATUS errors from repeated failures. It breaks
the valid contiguous tail rather than filling holes from old data. Three
consecutive SPI/CRC/status errors escalate; event/raw overflow or sequence
integrity failure is fatal. DRDY stall threshold is5000us, evaluated when the
owner task runs (not a proven physical shutdown deadline). LED recovery stops
scan generation, forces recovery/OFF, stops ADC, and returns STOPPED or FAULTED;
it does not automatically resume an interrupted scan.

V4 ADS faults immediately for SPI read, CRC, invalid status, unexpected resync,
epoch/sequence errors and event/raw overflow/backlog. First fault code/origin/error
is preserved. Local ADC fault disables capture/DRDY and releases transport.
This local action alone must not be equated to a proven whole-system LED-off
response: cross-subsystem propagation also needs inspection/testing. Its explicit
scan stop drains scheduler/ADC/optical/frame/network with bounded acknowledgments
and freezes a session report. Network drain is separated from safe LED shutdown.

NN2 ADC similarly fails closed on CRC/SPI/status/sequence/service ambiguity.
The service checks include30ms no-notification detection,250us service-gap logic,
and125us edge-age/read-crossing rejection. These are different conditions, not
three interchangeable DRDY period limits. Queued good records can still be
returned after the ADC has faulted, before an empty-queue IO error reaches pairing.
Thus ADC fault -> LED cancellation is not demonstrated as instantaneous.
The outer pairing fail path cancels LED and requests acknowledged ADC STOP;
restart needs explicit host clear/start, and poisoned SPI requires reboot.

Under-K/unequal valid-count pairs are data-quality events in feat35 and do not
by themselves stop a scan. Completed-frame/UDP pressure drops/counts frames while
acquisition can continue. Raw integrity loss has a different action.

## CRC layers (do not conflate)

| Layer | V3 | V4 | feat35 |
|---|---|---|---|
| ADS output SPI frame |CRC16,poly1021,seedFFFF|Same|Same, with padding checks|
| Coverage |First15bytes:STATUS+CH0..3; CRC at15..16|Same|Same; low CRC word pad17 expected0|
| ADS command-input CRC |Disabled in configuration|Disabled|Disabled|
| Binary firmware-host record |CRC16/CCITT-FALSE, header+payload, CRC bytes MSB-first|Same stream-v1 style|CRC32/ISO-HDLC, NNS2 header+payload, CRC LE|
| Control |ASCII legacy control outside binary CRC envelope|ASCII stream control outside binary CRC envelope|NNS2 requests/replies also protected by CRC32; USB ASCII separate|

A good UDP CRC protects the bytes sent, including wrong bytes if firmware
misclassified a sample before encoding. A good ADS CRC does not prove correct
phase, uninterrupted sequence, correct analog input, correct gain/calibration or
successful optical settling. CRC is not authentication or encryption and does
not recover missing UDP datagrams. Check CRC, sequence, status, timing and quality
as separate dimensions.

Concrete feat35 observation: integrity() combines CRC and zero-padding tests;
exchange() counts any such failure as crc_bad. It is not a pure numeric CRC
mismatch counter. Invalid runtime status becomes the generic READBACK fault;
no dedicated status-bit/F_RESYNC reason snapshot is published. A reset failure
can replace the original ADC reason. V4's code/origin/raw dump is more diagnostic.
The Python client rejects bad network CRC but its request loop catches and
silently discards ValueError; it does not increment a separate host CRC counter.
SequenceTracker exists but is not wired into run's frame_sink for a session summary.

## Sending data and finalization

V3:UDP control3333, binary destination3334; SESSION/TIME64/FNIRS_CFG metadata;
paired ON/OFF FNIRS_FRAME records (legacy layouts). Integer means with quality masks.
V4:UDP3333 stream-only control; destination3334; default one richer V4 frame,
optional two legacy ON/OFF records plus V4 frame. Carries quality/reason and
ON/OFF/corrected values; metadata and periodic TIME64 are sent. Network queue
and send failures have separate counters. In the rich V4 packet, n_on/n_off
are available counts, while the transmitted means use the selected tail of at
most10 samples; those fields must not be assumed to be the averaging denominator.
Legacy underk_mask uses <K_target,
so it must not be interpreted as identical to the newer minimum-valid quality.
NN2:binary command/reply and FRAME envelope to the locked owner IP+source port;
ACQ1 has1156 payload bytes /1188 total. Four-channel sums/counts and policy
metadata remain explicit; correction is on host. Frame sequence, envelope
sequence and ADC sequence are distinct. No retransmit of FRAMEs is implemented.

The NN2 four-frame output queue is producer-dropped/countable; network send
failures increment a transport counter, not ACQ1's producer_dropped field.
The host cannot interpret that one field as all end-to-end data loss.
NN2 currently sends no final SESSION_REPORT despite reserving a message type.
The current frame finalizes upon a raw sample entering the next frame; STOP
resets pending output and counts/discards the tail. V4 has explicit finalization,
drain and a frozen report. Exact frame accounting for a requested60-second run
therefore needs defined start/end and tail semantics, not simply Fs*60.

## Brainstorming priorities -- proposals only, no changes made

1. Adopt V4-style startup qualification: measured SPS/period, channel configuration,
   calibration readback, bounded expected-resync handling and diagnostic capture.
2. Choose error policy by class: fail session on uncertain raw alignment; invalidate
   an isolated quality event; drop/count transport congestion. Decide separately
   whether an isolated CRC error permits a new valid tail in the same session.
3. Preserve first-fault layer/code/time/sequence/raw STATUS and expected/received CRC.
   Separate CRC mismatch, padding/framing fault, status fault and transport loss.
4. Decide first-K versus newest-K from timing/settling evidence and board waveform;
   do not replace first-K merely because historical code used newest-K.
5. Assess the per-sample RPC path versus a bounded batch port/SPSC consumer while
   preserving one ADC owner. Measure service latency, drain latency and queue highs
   before choosing core/priority changes or larger buffers.
6. Define immutable session metadata, explicit normal-stop/tail policy and final
   report with separate acquired/valid/enqueued/sent/dropped/CRC-host counts.
7. Strengthen observability through the active protocol rather than copying V4's
   many serial commands wholesale. Keep low-level diagnostics outside the RT path.

Decisions to discuss: strict versus tolerated isolated CRC; K selection; equality
of ON/OFF counts; minimum startup gate; host raw capture versus sums/counts;
STOP tail handling; required fault report; host loss accounting. No decision here
amends the accepted timing/current/ownership contract. Fs minimum stays2Hz,
LED runtime800kHz and power100kHz remain unchanged.

## Source anchors (relative to the product checkout)

- reference/v3/components/nocti_v3_common/include/nocti_v3_version.h
- reference/v3/components/nocti_v3_common/include/nocti_v3_config.h:68,147,183
- reference/v3/components/nocti_v3_adc/nocti_v3_adc.c:219,325,408,717
- reference/v3/components/nocti_v3_adc/nocti_v3_ads131m04.c:80,389
- reference/v3/components/nocti_v3_optical/nocti_v3_optical.c:497,881,999
- reference/v3/docs/V3_ARCHITECTURE_CONTRACT.md; V3_FAULT_RECOVERY_TABLE.md
- reference/v4/components/nocti_v4_common/include/nocti_v4_common.h
- reference/v4/components/nocti_v4_common/include/nocti_v4_ads_config.h
- reference/v4/components/nocti_v4_ads/nocti_v4_ads.c:715,918,1033,1200,1614
- reference/v4/components/nocti_v4_optical/nocti_v4_optical.c:293,564,651,846,1010
- reference/v4/components/nocti_v4_network/nocti_v4_network.c:211,224,352,393,470,716
- reference/v4/main/app_main.c:495,541
- main/adapters/nn2_ads131_logic.c; nn2_ads131_esp.c; nn2_ads131_control.inc
- main/adapters/nn2_acquisition_esp.c; nn2_led_bringup.c; nn2_rtos_config.h
- components/nn2_core/src/nn2_core_acquisition.c; nn2_core_protocol.c
- components/nn2_app/src/nn2_app_comm.c
- components/nn2_net/nn2_net.c
- tools/nn2ctl.py:137,163,322
