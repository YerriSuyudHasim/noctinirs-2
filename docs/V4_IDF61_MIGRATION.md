# Owner-directed V4 continuation on ESP-IDF 6.1

Local source candidate, 2026-10-29 KST. Owner scope: use V4 scheduler/SPI/I2C
as the implementation base and selectively retain useful feat35 improvements.
No commit/build/flash/merge was authorized for the author in this iteration.

The independent project is `firmware/v4-idf61`. Existing feat35 working-tree
changes and the standalone checkpoint remain preserved. Repository-root builds
and existing CI continue to target feat35; this document does not silently
switch them to a new firmware or imply CI coverage of the migration.

## What the two lines of work implement

| Area | V4 | feat35 / current repository | Migration decision |
|---|---|---|---|
| RT acquisition | Complete scheduler, async emitter, ADS ingest and optical consumer | New adapters and integration serving NN2 contracts; several V4-derived fixes | Use actual V4 runtime component sources |
| Protocol | V4 console and UDP frame/control format | NN2 ADR0004 host control, nn2ctl and ACQ1 pairing | Keep V4 protocol initially; NN2 adapter is later explicit work |
| Structure | ESP-IDF components around hardware/runtime responsibilities | Core/Ports/Application/Adapters and architecture gates | Keep migration isolated; no wholesale root-layer rewrite |
| Configuration | Original V4 board and fixed10Hz optical geometry | Active board overlay,16MiB layout,240MHz, stopped-only Fs selection | Adopt reviewed board overlay and scoped flexible-Fs extension |
| Diagnostics | Original status/health and loss accounting | Startup failure evidence, CR/LF repair, runtime investigations | Retain V4 diagnostics first; add only demonstrated missing evidence later |
| Verification | Historical user bench reports | Host/architecture/CI work and user standalone evidence | Preserve provenance; no historical result qualifies this new image |

These tasks overlap in hardware acquisition but do not have identical product
scope. Feat35's integration failures do not invalidate all protocol, test,
configuration and architecture work. They also do not qualify that integration.
Our recent task-pipeline adaptation was still a mixed implementation, not a
complete V4 source port. The new project makes that distinction explicit.

All11 V4 components and main were staged with per-file source/active SHA256 and
adaptation reasons. Differences and build/protocol instructions are in the
project README. Default10Hz preserves original V4 timing geometry. Flexible
2/4/5/8/10Hz is a selected extension, not inherited historical evidence. Its
raw-stage capacity increases128 to256 records to hold a2Hz slot plus the original
32-record ingest batch; RAM and scheduler margins remain target measurements.

Static author checks:37 source hashes, component graph closure, selected board
pins/controller, partition overlay, credential exclusion, Fs geometry/window
budget, diff whitespace and319-file historical integrity. No compiler, linker,
unit execution or board results. IDF6.1 API compatibility is not confirmed.

Next gate is an owner build from the new project folder, then an approved V4
bench procedure using its own console/protocol. Do not use nn2ctl as if it were
compatible. Wi-Fi needs a locally configured password; empty config explicitly
leaves network unavailable. Build warnings, size/IRAM/RAM, reset behavior and
coexistence must be recorded. RTOS/hardware specialist and independent review
remain required before acceptance or merge. Author cannot provide independent
review of this migration. Recovery preserves old project/build and standalone
checkpoint; do not discard the existing working tree.


## Owner build feedback: uint32_t formatting

The owner's first IDF6.1 build reached scheduler compilation and rejected two
`%u` period fields after the flexible-Fs extension changed their type to
`uint32_t` (Xtensa `unsigned long`). The local correction uses PRIu32 in
SCHED_TIMING and in SCAN_OFF_PHASE's scaled-value error message; inttypes.h was
already included. The migration manifest was refreshed. No warning suppression
or replacement SDK driver was introduced.

Read-only inspection of the owner's generated project_description.json confirms
ESP32-S3, SDK path C:/esp/v6.1/esp-idf and all11 V4 components in the resolved
build graph. All16 declared external component directories/CMake registrations
exist in that SDK, including SPI/I2C/GPTimer/FreeRTOS. This is dependency-resolution
evidence, not successful link or full API qualification. Source/provenance,
whitespace and historical319-file checks passed after correction. The author
ran no build/commit/flash; owner rebuild and subsequent validation remain pending.


## NN2 communication binding requested after old USB reply

Owner reported STATUS-only intake with `ERR unsupported; SoftAP-only; use STATUS`.
Source search locates that exact reply in root components/nn2_provision, not the
migration console. This identifies an old image/project/port path; local edits
alone cannot change the flashed firmware.

The migration now defaults to a communication-only NN2 binding. Six root
components (core/ports/app/wifi/net/config) are reused directly through explicit
EXTRA_COMPONENT_DIRS. The single NN2 App/UDP owner runs Core0/priority4, requested
stack16384 bytes; a Core1/priority1 worker (8192 bytes) serializes V4 start/stop.
Main stack is12288 bytes and WPA2/WPA3 SoftAP settings match feat35. Tick1000Hz,
V4 priorities22/21/20, SPI and optical sample policy remain. Old V4 networking
is not initialized in this mode; two command owners never control the runtime.
USB becomes read-only diagnostics plus emergency off, which is forwarded to
the NN2 App owner to revoke its session. INFO reports V4_IDF61_NN2_CTRL.

New local v4_nn2_control.inc binds nonblocking START/STOP/state to V4 setup,
qualification and scan APIs. Stopped-only Fs and per-source currents are applied
before ADC START. Generation checks and a board-level reset-release gate prevent
a cancelled startup from releasing reset. STOP closes the gate and disables
async normal submissions immediately; cleanup allows all-OFF configuration-only
restore after scheduler/ADC/optical stop, then closes the gate again. Uncertain
I2C ownership/drain failures remain faulted, not reported as stopped success.
Hardware-fault recovery is intentionally not hidden behind protocol CLEAR.

Two scoped IDF6.1 compatibility changes accompany this binding: its I2C driver
calls on_trans_done without using the returned wake flag, so V4 callback now
explicitly yields when a higher-priority task wakes; callback detach also refuses
while the direct async transaction remains pending even after queue-drain success.
This retains handles/payload storage when completion was not actually observed.

Native V4 frame values lack full ACQ1 timestamp/sequence metadata. No fabricated
conversion was added: INFO says frames=unsupported. nn2ctl info and timed run
--led are the first control tests; --acquisition is rejected explicitly. Native
V4 optical processing and its retained reports remain. A measured-data wire
binding is separate work, not a claim of this control bridge.

Author checks: refreshed historical/active/generated/shared-source hashes,
component/provenance checks, INFO capacity, borrowed clock lifetime, whitespace,
319-file historical integrity. No compile/link, host execution, flash or board
operation. New RAM/stack/lease/cancellation/coexistence paths need owner build,
regression/board validation and specialist/independent review before acceptance.
The user's existing local sdkconfig was updated for new communication/WPA3/stack
settings, since old explicit sdkconfig values override defaults. Build must run
from firmware/v4-idf61; repository-root build still produces the prior firmware.

During manifest refresh the author's script briefly wrote the manifest to the
clean shared nn2_config.c file. The pre-write SHA256 was checked against HEAD,
that exact content restored, and the manifest saved to its intended path. The
shared source is unchanged and all manifest checks pass after recovery.


## Start-over branch checkpoint

Owner authorized a new relevant branch, commit and push for this version.
Branch: feat/35-v4-idf61-restart. Start-over note: continue from the actual V4
scheduler/SPI/I2C/runtime on IDF6.1 with documented modifications, rather than
continuing the mixed feat35 implementation. Commit df796ba preserves the prior
local feat35 work; the following migration commit establishes the new source
baseline. The standalone checkpoint remains available separately.

The active migration project is firmware/v4-idf61; root firmware is retained
for history/recovery. Validation attached to this source checkpoint is static
only: provenance/shared-source hashes, source graph/configuration/geometry,
staged whitespace and historical319-file integrity. The author's build/link,
unit execution and hardware tests remain NOT RUN. CI results are not yet known;
no independent review, merge or release approval is claimed. Local sdkconfig,
credentials, SDK installations and generated build artifacts are excluded.


## Owner build feedback: inactive native network password symbol

With NN2 communication enabled, the dependent native-V4 password string is absent
from sdkconfig.h. The still-compiled native network component referenced it
unconditionally. The header now uses the configured string only when defined,
otherwise an empty string. The existing8-63 character validation still rejects
initializing native networking without credentials; no open AP or default
password was added. NN2 networking continues using its own configuration.
Manifest, whitespace and historical/source checks were refreshed. Author did
not build, commit or flash this correction; owner incremental rebuild pending.


## As-is synchronization checkpoint: user bench boot82

Owner requested commit/push of the current branch without further functional
changes, recording the unresolved service-latency failure for external analysis.
The latest user report identifies baseline V4_IDF61_NN2_CTRL, boot82; exact binary
hash/toolchain/build warnings/size were not supplied, so no exact source-to-image
identity is claimed. The user built/deployed/tested the candidate; the author
performed no build, flash or hardware operation.

Reported INFO succeeds and START is accepted as STARTING. The first scheduled
LED ON is submitted and completed, then ADC faults with EVENT_BACKLOG at
PROCESS_EVENT. Rate qualification:1024 samples,8000SPS, period124-125us. Completed
SPI transactions:32-45us, average33us. CRC/SPI/status error counters zero. DRDY
event maximum depth4, missed_backlog3, raw-ring overflow0. The optical scan report
records3085us and no completed frame. Scheduler event/completion errors are zero;
async exit_failures1 is retained and control status reports ESP_ERR_INVALID_STATE.
The initial fault is ADC backlog; exact CPU/interrupt blocking source remains
unproven. PSRAM is disabled in both historical V4 sdkconfig and the migration;
this does not establish a capacity-related root cause. Sample-service latency,
priority22/21 work and SDK interrupt-masked paths remain investigation targets.

This commit also records the already-applied missing native-network password
Kconfig guard. Empty fallback does not enable an unsecured native AP. No further
runtime or threshold changes are made for this synchronization request.

Static checks passed: migration/shared-source provenance, component/board/config
and Fs geometry, whitespace, historical319-file integrity. Host regressions and
author build/link remain NOT RUN. Board integration is FAILED, not qualified;
CI/specialist/independent review remain pending. The owner additionally requests
an analysis of task responsibilities/service-time budgets and a future terminal-
only command/diagnostic interface compatible with verified stable feat35 control.
That command expansion is not implemented or claimed by this checkpoint.
