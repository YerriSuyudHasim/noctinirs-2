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
