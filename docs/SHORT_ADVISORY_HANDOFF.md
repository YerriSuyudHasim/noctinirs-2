# Agent 03 SHORT advisory implementation handoff

Acting as Agent 03 (Firmware Developer), model: Codex/OpenAI,
session: agent03-short-20261027. Assigned by Agent 01 in
[issue 33 comment 6439754275](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6439754275).
This is author validation, not Agent 06 verification or Agent 07 independent review.

## Source and scope

Repository: YerriSuyudHasim/noctinirs-2. Branch: feat/33-board-bringup.
Actual starting HEAD: 17db8216e0de864da8f41960f60a5b7682285d91; starting working
tree was clean. No reset/clean/stash or historical edits. Root sdkconfig SHA256
before/after: 3b1d138861eea9447c4a0516f82953281f00c9498a06e5868f3203bd6f840884.
The final issue handoff identifies the final local commit and exact-SHA reruns.
The companion evidence inventory fingerprints active inputs separately from the
base SHA: [source inventory](evidence/short-advisory-2026-10-27.json). Earlier evidence files are preserved and retain their original scope.

Implemented: explicit optional board-specific SHORT classification; fresh frame
scratch, complete/fault evidence; observed OPEN/code3 stops; unchanged one-hot
full ON/OFF images and unused-off/current/bus limits; Fs5 / 30 mA advisory restriction;
absolute 2 s cap and boot-lifetime single accepted attempt; cancellation interlocks;
quiet cached status and capability and CLI guards; English ADR/operator documentation.
No flash, device/COM-port access, energized work, physical fault injection, merge,
tag or release. The optional overlay is not applied to root configuration.

Changed paths are the board PCA source/header; LED owner source/header; optical
port capability and application INFO; composition-root STATUS and bounded serial
buffer; Kconfig and new sdkconfig.led-short-bench; CLI and source-identity tool;
LED/communication/CLI tests; ADR0006, LED bring-up and policy-review documents;
this handoff and its evidence inventory. The final Git diff is the exact inventory.
No domain-to-platform dependency or new external dependency is introduced.

## Acceptance disposition

| Acceptance | Author evidence / result |
| --- | --- |
| Strict default, advisory opt-in and visible policy | Separate native builds; strict retains allowed Fs set; INFO/CLI tests PASS |
| Fresh complete SHORT=valid=0xFFC3FF, OPEN=invalid=0 | ERROR/pattern matrix, every active channel/code, missing frame 2, replay and retained complete/fault epochs PASS |
| Healthy ERROR-clear remains healthy; inconsistent/mixed evidence fails | Strict/advisory matrix PASS |
| Immediate observed OPEN/code3 stop; I2C/config/thermal failures stop | Per-channel and every-phase transaction failure injection PASS |
| One-hot/unused OFF/current/bus limits | Production register images checked in strict/advisory host model PASS |
| Cap equality/current count, earlier alarm, no heartbeat extension | Full400-alarm run, every phase at expiry, inflight completion and stale callback count PASS |
| One accepted attempt across cancellation/clear; no late rearm | Prepare/per-phase cancellation, protocol replay/STOP/safe-off/lease expiry, late-worker and timer/cleanup failures PASS |
| Bounded cached STATUS / silent warning streaming | Actual production serializer at maximum widths and tiny buffer,1000 cached reads without I2C, no warning/log/packet producer added PASS |
| Target build/link/size and IRAM | See final issue/evidence record; author static/link evidence only |
| Electrical/settling/thermal/timing/stack margins | NOT RUN; physical review gates |
| Agent 06 / Agent 07 / required CI on exact SHA | NOT RUN / pending assignment; author tests do not satisfy independence |

The full cap run completes nine frames and 399 edges; expiry wins before the
400th OFF work, asserts reset and records cap without a deadline miss. It does
not label the incomplete tenth frame complete. The first-fault snapshot is the
last coherently published evidence at stop; an inflight transaction may finish
later and update current scratch, but cannot overwrite the first stop snapshot
or rearm emission. On-demand LED serialization is pure and uses one locked copy.

NXP Rev1.2 section 7.3.14 requires eligible PWM and an ON channel. Prepare now
writes/reads PWMALL=255, preserving FULL_ON and IREF. Sampling moves from immediate
post-ON to the existing OFF boundary before writing OFF. No blocking settle wait
is added. The datasheet does not provide a guaranteed diagnostic-settling maximum
for this software schedule. MODE1/MODE2 are observed at frame end (nominal 200 ms
at Fs5), requiring specialist disposition separately from chip thermal shutdown.

## Commands and actual limitations

Native LED/communication suites use CMake 3.30.2, Ninja 1.12.1 and MSVC 19.50.35721.0,
x64 C11 /W4 /WX, through the installed Visual Studio developer environment:

```text
cmake -S test/led -B build/short-host -G Ninja
cmake --build build/short-host
ctest --test-dir build/short-host --output-on-failure
cmake -S test/communication -B build/short-comm -G Ninja
cmake --build build/short-comm
ctest --test-dir build/short-comm --output-on-failure
python -m unittest discover -s test/communication -p test_*.py
python test/communication/test_host.py --loopback build/short-comm/nn2_loopback.exe
python tools/class-s-gate.py
python -m unittest discover -s tests/architecture -p test_platform_boundary.py
python -m unittest discover -s tests/architecture -p test_manifest.py
powershell -File tools/verify-reference.ps1
git diff --check
```

LED CTest 2/2 and communication CTest 1/1 PASS. Python communication 31 cases:
24 PASS, 7 loopback cases skipped without the optional executable; the explicit
loopback run executes 13 cases with no skips and PASS. Platform boundary 5/5,
manifest 15/15, dependency gate and 319-file reference integrity PASS.

Additional broad checks were attempted and did not pass: architecture discovery
ran 52 cases with 15 failure events and 1 cleanup error. Causes include missing
host GCC/Clang, Windows/Bash newline/redirection handling, fixture directories and
a Windows temporary-directory lock. Gate-fixture discovery ran 59 cases with 7
failure events from Bash being unable to execute a Windows Python path. No gate
was weakened or fixture changed. A baseline comparison for these failures was
NOT RUN; compatible CI must resolve/classify them. The core ClassU suite failed
configuration because the installed Espressif Clang has no x86 host backend;
its product tests are NOT RUN. This is an explicit remaining verification gap.

Target environment: ESP-IDF 6.1 at fff9895c82d744c7237be8847347bdd1b07c6643;
Xtensa ESP GCC 15.2.0 / esp-15.2.0_20251204; ESP32-S3; current main-board schematic
2026-10-16, assembled board revision unverified. Use the installed EIM environment
`C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1` on this PC; generic
export.ps1 initially failed due to installation-layout discovery. Python dependency
checking passes under the EIM environment. No toolchain/dependency installation
or safety/check bypass was used. One intermediate target build exposed an unused
local image-formatting buffer in the main loop; it was removed before final rerun.

Build into separate directories, preserving root sdkconfig:

```powershell
$env:SOURCE_DATE_EPOCH = (git show -s --format=%ct HEAD)
idf.py -B build/short-idf -D SDKCONFIG=build/short-idf/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup;sdkconfig.led-short-bench' -D IDF_TARGET=esp32s3 build size
idf.py -B build/strict-idf -D SDKCONFIG=build/strict-idf/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup' -D IDF_TARGET=esp32s3 build size
```

Before review, reconfigure both directories at the final SHA, confirm generated
advisory y / strict unset, PSRAM unset and GPIO/GPTimer IRAM/cache-safe options y.
Inspect ELF symbol sections and map: alarm_callback, stop_locked, gpio_set_level,
gptimer_get_raw_count, gptimer_set_alarm_action and
 timer_hal_capture_and_get_counter_value must be IRAM; LED lock/status/armed data
must be internal DRAM. Inspect disassembly/callees and include paths/config hashes;
record ELF/bin/map hashes, image size and warnings. Link placement is not latency
or electrical evidence. A clean two-build byte-reproducibility comparison was
NOT RUN: repeat each profile in two fresh directories at the same source SHA,
SDK, defaults, SOURCE_DATE_EPOCH and normalized path settings, then compare all
bootloader/partition/application images. CONFIG_APP_REPRODUCIBLE_BUILD remains y.

## Review requests and remaining gates

Agent 01/Nori: assign scoped Agent 04 Hardware and Agent 05 RTOS delta reviews on
the final SHA. Hardware: fault-response relaxation, PWM detection eligibility,
settling, same-board assumptions and residual matching-short risk. RTOS: shared
interlocks/ISR APIs, absolute cap,200ms health interval, failed-path timing maxima,
serialization/stack footprint and teardown races. Assign a separately accountable
non-author Agent 06 to reproduce tests, resolve broad-suite gaps and obtain exact-SHA
CI. Request Agent 07 from an eligible non-OpenAI family, or eligible distinct Grok
reviewer. These requests are pending, not review approval. A local commit must be
made available to those reviewers before exact-SHA remote review can complete.

High sink voltage is the Owner-approved implementation premise, not measured
voltage/current and not proof of safety. A real short with the same signature can
be accepted. Before any flash/energization, require reviewed numeric duration,
current/rail/temperature limits, a complete energy-disconnection method including
USB/battery, isolated nonhuman optical fixture, matched image/config/recovery and
specific Owner confirmation through Nori. CPU/interrupt failure can exceed the
software ceiling. No clinical or optical-exposure claim is made.

Rollback: omit the SHORT overlay and rebuild matched strict source/client. This
restores strict ERROR response but does not alter a running image or prove emitters
are physically off. Preserve evidence before recovery. No partition, security,
eFuse, charger, supply, current or bus-clock increase is included.

Next action: Agent 01/Nori assigns final-SHA specialist, non-author verification
and eligible independent reviews, carrying forward the explicit physical gates.
