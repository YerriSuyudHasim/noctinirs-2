# ADR-0008: V4-derived ADC transport/service and 16 MiB acquisition layout

Date: 2026-10-29 KST. Status: implementation candidate for review.
Accountable activity: firmware implementation and author tests, issue #35.
The Owner requested careful adoption/development from working historical V4,
then explicitly required using the 16 MB flash rather than 2 MB. This extends
ADR-0007's adapter/service decisions and its earlier partition-change exclusion.
Hardware/RTOS specialists and independent non-author review remain required.
No device operation, merge, release, or technical approval is granted by this ADR.

## Evidence and problem

Boot 38's first response was a one-bit-offset reset acknowledgment with failing
CRC; the CS-setup candidate subsequently produced boot 42, image
`b90685413e0be63056cddb4a5ac9b06c29b768cb9dcde85f8f95670177ff0319`.
The Owner reported `adc_crc=0 led_attempt=1 adc_fault=7 adc_lost=26
adc_missed=25 adc_unknown=1 adc_stop_ack=1`. Initialization/start reached LED
start, then GAP faulted acquisition. These are user-reported board observations,
not independent timing measurements. They do not establish the precise scheduler
delay or total number of physical conversions lost.

The active ADC task was priority 5 under LED priority 22 on core 1. The queued
SPI completion and notification-count-only service added avoidable scheduling
dependencies to a 125 us ADC period. A task notification mixed command bits and
DRDY counts, while its latest-edge latch could not preserve individual events.
The earlier diagnostic only logged startup failures, leaving runtime GAP without
a corresponding detailed console record.

## Historical provenance and selected adoption

Read-only V4 source: `reference/v4/components/nocti_v4_ads/nocti_v4_ads.c`
(`transfer_frame`, `transport_acquire/release`, `drdy_isr`, `wait_for_event`,
`start_dark_internal`, `process_event`, `ads_task`), and its ADS configuration.
Integrity is checked before/after. No historical source is a build input or
modified. This is a scoped adaptation, not a wholesale copy of its runtime.

| V4 pattern | Active adaptation |
| --- | --- |
| Polling SPI, explicit complete frame, bus ownership | Full-duplex 144-bit static TX/RX descriptor; mode 1, 8 MHz, DMA off, explicit 50% duty and CS 1/1. Acquire the existing dedicated SPI2 before configuration transfer; hold through READY/running; reset/STOP releases after successful quiescent reset. |
| Polling completion | Use IDF `polling_start` + finite `polling_end`, retaining the existing 20 ms completion-wait request. Unlike unbounded `polling_transmit`, completion timeout poisons the transport. |
| ISR records work; notification wakes owner | An eight-entry timestamp/serial event ring, guarded by the existing ISR/task mux. Notification uses `eSetBits` as a doorbell. Service counts only ring records plus explicit overflow, never doorbell values. |
| Capture gates around session transitions | Capture is disabled/cleared before reset and SYNC; enabled after successful SYNC. Serial baseline is taken before SYNC so an early first edge is retained. Serial continues modulo 2^32; stale session records are cleared. |
| Complete initial settling in ADC owner | Service/discard the first two fast-start frames before START acknowledgment. CRC/padding remain mandatory. Wait budget is min(caller deadline, 20 ms); absence of events faults without acknowledging START success. |
| Deadline-first acquisition scheduling | ADC priority 23, core 1, above this project's LED 22. V4's literal priority 20 would remain below LED here, so its number is not copied. Stack stays 4096 bytes. |
| Explicit fault provenance | Boot-lifetime first runtime fault retains pending/dropped events, expected/event/observed serials, edge/failure/last-service times, original fault and cleanup result before reset. Emit once as `ads131_runtime`, outside the sample-success path. |

All I/O and synchronization remain in the outer adapter. The new event source
callback and ring header are adapter-private, using existing framework-free
record types. Core/Application/port ABI and USB frame/STATUS layout do not change.
The existing serialized control mailbox, outward deadline checks, poisoned-storage
lifetime, watchdog, raw queue, quality flags, cancellation interlock and LED
power sequence remain in use. The count-only path remains a legacy host seam;
the production adapter always supplies the recorded-event source.

## Integrity and timing limits

An event ring cannot reconstruct ADC samples overwritten in the device. This
candidate retains the stricter active policy: multiple pending events, overflow,
serial discontinuity, event age >=125 us, or a new edge during SPI fault instead
of reading old samples as current. Eight event slots fit the unchanged static
12 KiB device budget; V4's 128-entry event buffer and FIFO catch-up allowances
are not imported without measured association evidence. Overflow remains counted.

Readiness and frame STATUS requirements, first RREG/reset response handling,
MODE=0110/level DRDY, reset/SYNC delays, CLKIN/OSR/rate/gain, pins, and channel map
remain the active board contract. LED runtime remains 800 kHz, configuration/power
100 kHz, optical Fs minimum 2 Hz. There is no bit shift, CRC exemption, dummy
clock, or relaxation of GAP checks. Time is sampled after event/latch snapshots
so an ISR arriving during the snapshot cannot create a false future timestamp.

IDF v6.1 only accepts `portMAX_DELAY` for bus acquisition and polling start.
The dedicated sole-owner bus has no other queued users; acquisition/start is
not claimed to have a hard finite lock bound. Polling completion remains tick
bounded (100 Hz tick rounding and driver's strict `>` check can exceed the
requested 20 ms). Polling at priority 23 can delay lower-priority work during
a stuck transfer. Timeout keeps the descriptor/buffers/bus alive until reboot;
reset/STOP does not claim cancellation of a pending driver transfer. These facts
need RTOS/Hardware review and bench fault-timing measurement.

The ISR remains under the existing GPIO ISR-service/cache policy. No cache-off,
interrupt-latency, stack-watermark, CPU-budget, sustained 8 kSPS, optical timing,
or physical fail-safe claim is established by the host tests/build.

## Use of the 16 MiB flash

The profile checker exposed a pre-existing stale external build configuration:
16 MiB acquisition defaults had been overridden by 2 MiB generated settings.
The earlier successful compilation did not satisfy the acquisition profile check.
The failed check and intermediate builds are retained as evidence.

The versioned acquisition overlay now selects the custom table
`config/partitions/acquisition-16mb.csv`. NVS stays at 0x9000/0x6000, PHY at
0xF000/0x1000, and factory app starts at 0x10000, preserving the prior locations.
The factory partition spans 0xFF0000 bytes to the exclusive flash end 0x1000000
(16 MiB). This uses the remaining address space for the existing single-app
design; it does not invent an OTA, filesystem, or measurement-data format.
The unused portion remains capacity, not a claim that the current image needs it.

Branch validation and profile checks require 16 MiB and the versioned custom
table. Compiler guards reject acquisition with 2 MiB or a non-custom table.
Source fingerprints include the acquisition overlay and partition source.
SDK partition verification must pass and the generated binary table must match
the intended ranges. No erase, NVS migration, partition flashing or device
repartitioning is performed here. Applying this layout on hardware is a separate
reviewed/authorized action; an app-only flash would not replace an old table.

## Acceptance, tests and rollback

Author tests exercise the actual polling implementation through mocked IDF calls:
bus retention/release, descriptor pointers/144-bit lengths, lock/start errors,
completion timeout, unchanged RX on failure and poison/no-reuse. Production logic
tests cover boot 38 bytes, boot 42-like 26-event pressure, overflow accounting,
ring/serial wrap, stale/crossed events, early SYNC edge, before-ACK settling,
startup timeout, runtime snapshot retention, and caller deadline regressions.
Profile tests include 2 MiB/wrong-table negatives and production compile-guard
negatives. Native tests are not a simulation of ESP scheduling or ADC electrical
behavior. Final counts, hashes, compiler/SDK versions, image sizes, map/device
size, config, warnings and exact commands are retained in
`ads131-v4-revision` beside the Orchestrator workspace.

Remaining acceptance requires separately authorized board measurements: confirm
the new image/config/table identity, first-response CRC, uninterrupted DRDY/read
association under LED/SoftAP load, runtime fault timing, STOP acknowledgment,
bus cleanup, and explicit restart. No hardware result for this revision is claimed.

Rollback is a reviewed revert to the archived boot-42 candidate. If a new
partition table has later been installed, restoring source/app alone does not
restore that table; preserve its exact image and use a separately approved
procedure. NVS offsets are intentionally unchanged; never erase user settings
as part of an automatic recovery. No merge/release/flash occurs in this task.

## Local author validation summary

- Native suite: 90 tests, zero failures/ignored, MinGW TDM GCC 5.1.0,
  strict C11 and Unity 64-bit assertions enabled. Canonical Unity hashes pass.
- Control regressions: reviewed legacy source reproduces all 11 expected
  failures; current control passes all 25 cases. Positive/zero-test controls pass.
- Generated acquisition profile passes; configuration negatives reject 2 MiB,
  single-app defaults and wrong custom table. Production compiler guards pass
  their positive case and all five negatives. CMake branch-profile positives
  and three negatives pass. Public-header opacity probes pass.
- Optical compile/link passes on ESP-IDF v6.1 / Xtensa GCC 15.2.0. App header
  is DIO/16MB. Generated partition table exactly preserves NVS/PHY/start addresses
  and extends factory to 16 MiB. Final post-commit image/hash/size are recorded
  in the evidence manifest rather than inferred from a base Git SHA.
- ADC device state is 12288 bytes, exactly at its unchanged compile-time limit.
  There is no remaining headroom in that particular static-device budget.
- Historical integrity passes for 319 files; whitespace checks pass.

Intermediate invalid-profile/missing-table attempts are retained; none is a
deliverable firmware claim. IDF 6.1's generated `# default:` annotations can reset
a manually changed string to its default: the custom table path is explicitly
selected, then verified in generated config and the compiled table. The IDE's
generated acquisition profile is backed up and updated to the same table.
Its generic/root sdkconfig and unrelated profiles are not overwritten.

## Local follow-up: boot 49 transport boundary and CPU budget

The Owner reported boot 49 failing on the first settling frame with
`serial_after_spi`, event serial 1 / observed serial 2, edge age 169 us and
`spi_elapsed_us=103`. The earlier timer included `exchange()` CRC work, so it
was not a measurement of transport alone. No CRC failure was reported.

The local candidate captures completion time/serial before CRC, validates each
runtime frame once, and consumes recorded events individually. Like historical
V4, at most two pending events are admitted; dropped events, a third pending
event, sequence discontinuity or an oldest-event age reaching 250 us fault the
session. A single following event is queued work, not by itself a GAP. The
legacy count-only seam retains its single-event/125 us checks. Owner draining
is bounded by the event-ring capacity before revisiting commands/watchdog.
This changes the earlier conservative single-event policy; it does not establish
physical FIFO/event association or sustained throughput without board testing.
Reference: TI SBAS890D section 8.5.1.9.1, and V4 process_event/process_runtime_events.

The acquisition overlay and explicit branch profile now select a 240 MHz CPU.
SPI remains 8 MHz, LED runtime I2C 800 kHz, power/configuration 100 kHz, optical
scan minimum 2 Hz, and flash 16 MiB. The existing local generated acquisition
sdkconfig is aligned to 240 MHz as well; it is not a versioned artifact.

Host regression sources cover the boot 49 timing, ordered two-event reads,
three-event rejection and event-ring wrap/pop behavior. Per the Owner's request,
these sources have NOT been compiled or executed, and no firmware build,
commit, push or hardware operation is performed for this local candidate.
The same nn2ctl run command and serial/STATUS capture are used for the next test.

## Local follow-up: align the ADC configuration with historical V4

Boot 51 reported serial 1 to 3 within169/176-us-scale first-read timing, with
no CRC failure and no LED start. This does not establish the physical origin
of the extra events. Review found V4 explicitly selects pulse DRDY, whereas
this candidate still selected level DRDY. The Owner requested alignment of the
active technique/settings with V4, keeping work local and without builds.

Aligned settings and sequences:

- MODE=0111: 24-bit words, CCITT output CRC, input CRC disabled, push-pull pulse
  DRDY; CLOCK=0F0A, GAIN1=0000, CFG=0600 and all four CHn_CFG=0000.
- Reset LOW300us, post-reset wait20us, explicit NULL/FF24 reset acknowledgment,
  UNLOCK, and individual register writes/readbacks. Active command exchanges
  keep CRC and the extra explicit WREG acknowledgment check.
- SYNC LOW5us with the GPIO interrupt disabled and the event ring cleared;
  capture is enabled before unmasking. STOP/reset disables the pin interrupt.
- Three checked/discarded startup frames, one expected F_RESYNC budget during
  settling only, and no discarded frame delivered to the acquisition consumer.
- Pulse readiness uses temporary interrupt capture instead of GPIO level polling.
- SPI remains mode1, 8MHz, 144 bits, CS guards1/1, duty128, default clock source,
  zero input delay, no DMA, one queue slot and sole-owner polling/bus retention.

Intentional active differences: board GPIOs/SPI2 stay unchanged (V4 uses SPI3);
ADC priority23 remains above LED22; active event/raw capacities and finite caller
budgets remain. V4's startup rate qualification, factory calibration readback,
100-ms waits and raw-ring overflow behavior are not imported in this patch.
Runtime freshness/status and250us age guards remain stricter than V4. CPU240MHz,
flash16MiB and the LED/power/scan limits remain unchanged. The reference snapshots
are read-only. This is scoped adoption, not a claim of full V4 equivalence.

Host test sources update the 36-exchange startup sequence, reset-ACK replay,
MODE/CFG/channel settings, three-frame settling and bounded F_RESYNC handling.
No test compilation/execution or firmware build is performed per Owner request.
The next board run uses the existing nn2ctl command; collect startup transport
configuration, any fault diagnostics and STATUS. No commit/push/flash is performed.

## Local follow-up: boot53 evidence, rate gate and ISR placement

Boot53 faulted at the retained service-gap guard: 261us since the prior transport
completion, although the new event was only31us old. Serial60 was consecutive,
with no recorded ring loss or CRC failure. This does not exclude a physically
suppressed/missed DRDY pulse, particularly if the preceding read overlapped a
conversion. The Owner authorized local diagnostic/qualification/ISR edits only,
with no build, commit, push or hardware operation.

The logic retains the last8 read attempts in a task-owned ring: edge/period,
transport begin/end, CRC duration, pending depth, status and phase. Failure freezes
that ring before cleanup and prints it afterward. Existing GAP/CRC/status guards
remain; timing evidence is gathered without granting unverified continuity.

Production START now discards3 settling frames and1024 qualification samples;
none of these samples enters the application ring. Acceptance requires mean
rate7600..8400 SPS, no observed interval over188us, and successful per-frame
integrity/sequence/backlog checks. The interval rule is an additional conservative
check beyond V4 mean-rate qualification; ISR timestamps remain software evidence.
The owner allows400ms within the caller's deadline, and acquisition START changes
from100ms to500ms. It refreshes the task watchdog while qualifying. LED starts
only after success and the acquisition permission/epoch check. A STOP request
arriving during synchronous START may wait for this bounded startup work before
the caller can submit STOP. Successful qualification is logged after STOP only.

The GPIO ISR uses IRAM_ATTR, scalar ring writes and internal DRAM device/lock
state; it calls only IDF critical-section, IRAM timer and FreeRTOS ISR-notification
paths. Checked official IDF6.1 timer source aliases esp_timer_get_time to the
ESP_TIMER_IRAM_ATTR implementation; SPI Kconfig requires FREERTOS_IN_IRAM for
SPI_MASTER_IN_IRAM. Both ADC overlays select those options and ESP_TIMER_IN_IRAM;
the ESP translation unit rejects stale configurations. The active generated
acquisition sdkconfig is aligned locally. Unknown preinstalled GPIO ISR services
are rejected instead of silently accepting unknown interrupt-allocation flags.
Linked call-graph/IRAM-map confirmation is NOT RUN and remains required after
the user's build; compiler-generated callees must be inspected there as well.

The bounded history and qualification state increase static ADC storage; the
compiler ceiling is explicitly raised from12288 to13312 bytes (+1KiB ceiling),
without changing stacks, raw/event capacities or using heap allocation. Actual
linked size and additional IDF IRAM/RAM cost are NOT RUN. Hardware/RTOS review
remains pending. CPU240MHz,16MiB flash and emitter/scan limits remain unchanged.

Regression sources cover qualification success/slow-rate rejection, discarded
samples, history wrap/freeze, the1027-read START handoff and startup timeout.
Per Owner request they have not been compiled or executed. Validation performed
here is source review, diff whitespace checking and historical-file integrity.
The same nn2ctl run command is used; collect ads131_rate and all ads131_trace
lines with fault diagnostics/STATUS, or the qualification summary after STOP.

## Superseding local candidate: complete ADC path and staged integration

The Owner requested a coherent V4-derived ADC path and isolated ADC testing,
then staged LED integration. See [FEAT35_V4_BASELINE](../FEAT35_V4_BASELINE.md)
for the current comparison, intentional adaptations, local defaults and test
commands. That document supersedes this ADR's earlier candidate statements about
250us recorded-event guards,raw overwrite behavior,ring capacities/13KiB memory
ceiling,priority4 consumption and unchanged LED test procedures. No build or
hardware result is claimed for the new candidate; the historical sources remain
unchanged. The first local build selects ADC-only dark baseline explicitly.


### Local V4-7.2 scheduler handoff extension (implementation candidate)

The Owner requested continuing from the V4 communication/task pipeline on IDF
6.1. The current local adapter adds `nn2_sched` on Core 1 / priority 21 between
LED 22 and ADS 20, with the pairing consumer on Core 0 / priority 12. This
implements the ADR-0003 ordering and scoped scheduler task role. Static deadline
and completion rings replace the combined LED phase/window owner; details,
provenance, retained NN2 differences and validation limits are in
`docs/FEAT35_LED_INTEGRATION.md` under the V4-7.2 adaptation section. There is
no new domain-to-framework dependency. Literal task/ISR timings and memory
margins are unqualified; source regression updates are not execution evidence.
No build, commit, flash, independent review or merge is claimed for this local
candidate. Existing real-time/hardware specialist and independent review
requirements remain before acceptance.
