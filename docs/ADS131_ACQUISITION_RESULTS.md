# ADS131 acquisition implementation results

Date: 2026-10-28. Role: Firmware Developer, author-side verification.
State: implementation complete in the available software scope; local review candidate.
No independent approval, CI execution, hardware validation or merge is claimed.

- Branch: `feat/35-ads131-acquisition`
- Base/main verified by fetch: `cbec2dab8e23595ae4e374e181c0d7c70f25edec` (PR #74 squash).
- Final tested firmware source: `4a493a7e18bb6108ef28d0edd59852cb290225ec`.
- Implementation: `859b8d6`; startup provenance fix: `453b691`; unequal-pair quality fix: `c6fcfd6`; measured memory-bound restoration: `4a493a7`.
- Existing local work outside this previously clean checkout and other checkouts was preserved.
- Contract/provenance: [ADR-0007](adr/0007-ads131-acquisition.md).
- Machine-readable logs, hashes and configuration identity: [evidence](evidence/ads131-acquisition-2026-10-28.json).

## Completed behavior

Existing ADS131M04 configuration/readback/CRC/int24 decoding is retained. Added
DRDY ISR timestamps and serial checks reject stale, coalesced or cross-conversion
reads. LED completion windows feed a single pairing owner, with500us provisional
analog settling,375us full sinc3 support and100us end guard. Classification uses
DRDY, never SPI-read time. K_target10/K_min8, equal same-slot ON/OFF counts,
warmup and invalid-pair metadata are explicit. Raw sums/counts survive invalid
pairs; host correction is mean(ON)-mean(OFF), in ADC counts only.

ACQ1 frames use the active NNS2 envelope/session/peer lock and nonblocking UDP.
Queue overflow, sequence discontinuity, CRC/status/SPI errors, STOP/fault cleanup,
acknowledged ADC stop and explicit restart are handled. Normal STOP discards and
counts the unfinished tail; no dark phase is reused across sessions.
Acquisition Fs is exactly{2,4,5,8,10}Hz after the Owner's minimum2Hz correction;
LED-only retains2..10Hz. LED runtime remains800kHz, power/configuration100kHz.
V3 int24/status/CRC separation and V4 DRDY/session, completed-window and local
pairing techniques were adapted without copying their runtime/hardware assumptions.
Schematic labels for all20 selected PCA outputs match the host source/wavelength map.

## Build and tests actually run

ESP-IDF6.1 commit `fff9895c82d744c7237be8847347bdd1b07c6643`, Xtensa GCC15.2.0,
ESP32-S3. Final compile commands for all three builds contain the final source SHA.
I2C SDK driver is unchanged, SHA256 `3ebaeee176e2ff731d15c69a7629c32c0339817afcd31e93da167f4f6b9d9ca0`.
Board identity is the source-defined Nocti v1/N16R8; physical fixture: NOT RUN.

| Profile | Build/link | App binary bytes | Flash setting | Compiler warnings |
|---|---|---:|---|---:|
| acq | PASS | 879,776 | "16MB" | 0 |
| led | PASS | 839,408 | "2MB" | 0 |
| diag | PASS | 219,376 | "16MB" | 0 |

Build command for each profile was `idf.py -B <build_dir> -D IDF_TARGET=esp32s3
-D SDKCONFIG=<config> -D SDKCONFIG_DEFAULTS=<overlays> reconfigure build`.
All commands used the existing SDK/venv, internal-memory configuration, finite
job counts and separate build/config paths under `C:/esp/ads131-20261028`.
Overlays: defaults+communication+led-bringup+led-short-bench+acquisition for acq;
the same without acquisition for led; defaults+ads131-diagnostic for diag.
The LED-only2MB image setting is unchanged; ADC profiles select DIO/16MB.
The ADC static state is11968bytes, within the unchanged12288-byte bound.

| Check | Actual result |
|---|---|
| Native production ADC/pairing/protocol/control suites |47 tests,0 failures,0 ignored at `c6fcfd6`; final ESP-only memory guard does not change native-tested behavior|
| Python communication/parser regressions |50 discovered,41 executed,9 skipped,0 failures|
| LED strict/advisory deterministic regression |1,245,709 /1,246,238 checks,0 failures|
| Acquisition LED window publication/cancellation/overflow seam |641 checks per strict/advisory profile,0 failures|
| Inner-layer include/CMake Class S gate |PASS|
| Manifest / workflow tests |15 /2 tests PASS|
| Generated config validator |Acquisition and diagnostic PASS; existing positive/7-negative selftest PASS|
| Historical references |319 files PASS before/after work|

Native tests used the installed MSVC19.50 C11 compiler and pinned Unity, with
production sources unchanged and a temporary runner under `C:/esp/ads131-native`.
Executed command files/logs are retained locally under `C:/esp/ads131-*` with
hashes in the evidence record. MSVC reports existing shadow/flow-analysis warnings;
zero host compiler warnings are not claimed. Native GCC/Clang full CMake Class U
was not available (ESP Clang lacks x86 code generation; MSVC lacks the existing
suite's max_align_t support). The9 Python skips and this full-suite limitation
are not counted as passing tests.

An initial diagnostic auto-target attempt selected esp32; it was stopped and
replaced with explicit ESP32-S3. Preliminary/interrupted builds and the initial
2MB acquisition image are not the final build evidence above. Logs are retained.

## Limits and next board procedure

No flash, serial/device control, energized test, release, push or merge occurred.
Timestamp latches are software estimates;500us analog settling and jitter margins
are provisional. ADC8kSPS plus LED800kHz coexistence, ISR latency, FIFO alignment,
stack watermark, heap margin, signal integrity and physical OFF remain NOT RUN.
Raw samples are buffered internally; the host stream preserves phase sums/counts,
not every individual conversion. No analog current/I_dark transfer function is assumed.

Next: obtain the scoped Hardware/RTOS and independent review, then use a separately
authorized board procedure to verify ADC clock/DRDY and known inputs with LEDs off,
followed by integrated edge/filter timing and K at Fs10, backpressure, STOP during
transfer, fault cleanup and repeat START. Prior LED-only bench results do not prove
ADC integration. Rollback is the default non-acquisition profile or a reviewed revert.

## Unpublished handoff

```yaml
handoff_version: 1
project: noctinirs-2
repository: YerriSuyudHasim/noctinirs-2
issue: 35
from_role: Firmware Developer
to_role: Integration Engineer
commit_sha: 4a493a7e18bb6108ef28d0edd59852cb290225ec
status: ready
summary: ADS131 acquisition software implemented; local build and host evidence only.
changed_paths: [components, main, test, tools, sdkconfig.acquisition, .github/workflows/firmware-build.yml, docs/adr/0007-ads131-acquisition.md]
acceptance_criteria:
  - id: acquisition-software
    result: PASS
    evidence: docs/ADS131_ACQUISITION_RESULTS.md
  - id: physical-coexistence
    result: NOT RUN
    evidence: No hardware operation authorized in this task.
tests:
  - name: affected-target-builds-and-host-regressions
    result: PASS
    evidence: docs/evidence/ads131-acquisition-2026-10-28.json
risks: [Unmeasured timing and coexistence, Full native GCC Class U not run locally]
assumptions: [Source-defined board pin and clock tuple; no analog current model]
open_questions: [No hardware input blocks this software candidate; bench qualification remains]
next_action: Review the source and software evidence before a separately authorized ADC-first board procedure.
requested_by: Human Owner in this chat
created_at_utc: 2026-10-28T07:37:56Z
```
