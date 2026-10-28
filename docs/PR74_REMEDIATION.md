# PR74 author remediation and verifier handoff

Date:2026-10-28 KST. Role: Agent03 Firmware Developer / implementing author,
OpenAI Codex, session agent03-ir74-remediation-20261028. Not independent review.
Authority: Owner's direct PR74 remediation instruction in this session, continuing
[the recorded assignment](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6439754275).
No external messages/comments, Ready transition, merge, release or hardware work.

## Candidate identity and reviewed inputs

Original IR head and local starting head:5c0fae99ee87c0abfac886af7e3aa7505817b08a.
Verified PR base:5958a0eafec6b03c2b158c4026f3f10b8a3366d1. Working tree initially clean.
Implementation remediation commit:9fc3ac7630420ed621245da306e4a15b0eeaf023.
This document and its evidence record are subsequent documentation-only changes.
A CI follow-up isolates a tooling unit fixture from the caller's historical Git
cache; it changes only the Python test, not firmware or production provenance pins.
The final submitted PR head is the containing branch tip, recorded in the author
handoff response and GitHub checks; verify with `git rev-parse HEAD` before testing.

Read: product AGENTS.md, current hub AGENTS.md, recorded issue33 assignment and the
complete Owner-supplied IR report. Technical findings mapped to the unchanged
reviewed head before edits. The old IR's provider-family restriction predates the
current hub wording: current eligibility requires a distinct assigned non-author
agent in an isolated review context. This author is ineligible. No independence
policy is changed in this firmware PR, and no approval is claimed.

## Findings and disposition

| IR finding | Disposition | Source / regression evidence |
| --- | --- | --- |
|1. Shared editor implicitly selects advisory800kHz | FIXED | .vscode/settings.json now selects only sdkconfig.defaults; generic Kconfig/defaults select FAKE with LED disabled. Explicit selector/backups and profile assertions remain. test_ir74_tools.ProfileValidation plus existing board-selector tests. |
|2. Profile matrix disagrees with FAKE-only validator | FIXED | Both build/reproducibility matrices retain explicit FAKE and diagnostic rows and add COMMUNICATION. sdkconfig.fake is explicit. check_ads131_config.py --profile enforces exactly one choice and communication prerequisites; diagnostic protections and negatives retained. |
|3. Collector includes platform files in inner-layer coverage | FIXED | collect_inner_build.py uses canonical layers.json via layer_contract.py. Host CMake uses the same paths and compiles all inner C sources; failure-control copies include required tools. Missing/duplicate header/source coverage and unreadable traversal fail. Supplemental adapter collector includes declared platform sources separately. |
|4. Workflow detector fixtures lack declared platform directories | FIXED | Both fixture setups create directories from platform_components.json. Missing-platform and platform-symlink controls added; existing unknown/missing/symlink negatives retained.61 gate fixtures and2 workflow tests pass locally. |
|5. SDK error may end without on_trans_done | FIXED, execution pending | Pinned driver analysis confirms error path. Fake now models no callback by default on NACK/TIMEOUT; synthetic error callbacks are only an extra defensive case. Production retains s_pending/buffers/handles and reports led_reboot=1 if lifetime is unproven. New native scenarios cover both no-callback errors, failed recovery/repeated cleanup and test-only MCU reboot. Source passes syntax-only; executable tests NOT RUN locally. |
|6. Historical blobs unavailable in CI checkout | FIXED, CI to confirm | Old job logs fail git show for original pinned commits. fetch-depth0 fetches advertised branch ancestry, not every unadvertised pre-squash object. Exact SHA fetch restores the three unchanged reviewed commits; every consumed blob is SHA256-verified. Empty Git-object-database probe passes. Original old-fail regression scripts and pins are unchanged. |
|7. Three extra blank lines at EOF | FIXED | Only trailing empty lines removed from PROJECT.md, nn2_config.c and DECISIONS.md. diff --check against the base is clean. |

FIXED describes the implemented author remediation, not non-author verification,
specialist disposition or merge readiness. No technical finding was dismissed.

## Pinned SDK error and recovery reasoning

SDK:fff9895c82d744c7237be8847347bdd1b07c6643, ESP-IDF6.1; inspected
components/esp_driver_i2c/i2c_master.c, unchanged from its Git commit.
The ISR clears trans_done; NACK/TIMEOUT branches select an error, while only the
MST_COMPLETE branch sets trans_done. s_i2c_send_command_async handles error by
marking the internal transfer finished and resetting its event; on_trans_done
remains inside the trans_done gate. A direct async transfer is not necessarily
represented in num_trans_inflight, which wait_all_done drains. Thus ESP_OK from
wait_all_done alone does not prove the direct transaction no longer references
our storage. Private SDK state is not used as an unsupported recovery API.

The unchanged timer/owner deadline cancels emission and asserts reset even if
no callback arrives. Cleanup retains pending state and exposes led_reboot=1;
CLEAR_FAULT/START remain blocked. Never clear s_pending just to unlock recovery.
A genuine late completion followed by successful drain/unregistration/removal
can prove quiescence and clear the flag; if no callback ever arrives, the Owner
must reboot the MCU. No automatic reboot, SDK patch or new error bypass is added.

Recovery procedure: retain cached USB STATUS, stop retrying RUN/CLEAR_FAULT,
keep emitters off, and let the Owner reboot using their existing approved method.
A reboot discards first-fault RAM evidence, so collect it first. This is a
procedure definition only; no agent device access or energized test was run.

## Historical-blob provenance

Original SHA/path pairs remain in tools/verify_c11_regression.py,
verify_ads131_control_regression.py and verify_repro_regression.py. The new
fetch_regression_sources.py reads tools/regression_sources.json, fetches only an
unavailable exact commit from origin, and verifies original consumed blob hashes.
It does not replace baselines, catch-and-ignore regression failures, or use current
source as fallback. The original regression-first controls still run afterward.

- C11:789b4fa8bb2904a85116c6c6116973c348565c05, original PR39 reviewed helper/guard;
  ancestor of remote refs/pull/39/head,036927a0b9d7159e3516c1bb4970e962383243cd.
- Reproducibility:f97767035e0ab4f801e655ec751141314668c407, PR40 reviewed helper/workflow;
  ancestor of remote refs/pull/40/head,686e606f49b41f4ea591826b9c73bf4b3973e11c.
- ADC:99ffe66e51f95a17bf62194d2b7216390c08aa33; original regression's exact OLD
  commit and063dad59eb1c439d7f592a6aa431e3e27f12f1a8ea273412287d1bedd57cf7c2
  source hash. It is not an ancestor of PR15's current head; no such provenance
  is claimed. Direct fetch by the exact SHA from origin succeeds and retains
  the original content-addressed commit, tree and blob. The verified bytes match
  the pre-existing regression hash. No new remote refs/tags were created.

Future removal of unadvertised historical objects by the host must fail closed;
a separately reviewed archive/provenance repair would then be needed. Current
availability was tested from an empty object database, not inferred from local cache.

## Fresh local verification and limits

All results below were run during this remediation against the implementation
committed as9fc3ac7, except subsequent documentation-only edits. See
[evidence](evidence/pr74-remediation-2026-10-28.json) for identity and limitations.

- PASS:8 new tooling tests (inventory omissions/duplicates/substitution, traversal
  failure, profile exclusivity/prerequisites, generic opt-in defaults and pinned
  blob mismatch/missing commit controls).
- PASS:61 gate fixtures, including missing platform, symlink and unknown controls;
  2 workflow tests including48 gate-invariant state combinations.
- PASS:47 discovered communication/profile tests:40 executed,7 native loopback skipped.
- PASS:5 platform-boundary tests,15 manifest tests and actual Class S static gate.
- PASS:33 reproducibility-tooling tests using Python/temp files and deliberate
  failing fake idf.py commands; no real firmware build or native compiler.
- PASS: original reproducibility regression-first control still exposes expected
  old failures and returns success only for those expected assertions.
- PASS: fresh exact-SHA fetch and5 original blob hashes across3 commits.
- PASS: MSVC19.50 /Zs C11 /W4 /WX owner/portable driver/native test source in both
  policies; Xtensa GCC15.2 -fsyntax-only against actual pinned IDF6.1 headers for
  STRICT400kHz and advisory800kHz. No object, executable, archive or linker.
- PASS:319 historical reference files before/after, whitespace checks.

Local native C execution, CMake configure/build with compiler probes, firmware
build/clean/rebuild/link, size/map/reproducible firmware builds: NOT RUN by agent
under the unchanged Owner restriction. Executable C regression cases are prepared,
not represented as executed. CI results belong to their actual head/run and are
reported separately; old CI and old bench results are not new candidate evidence.
Push may start the repository's existing automated CI, but the agent does not
manually execute or dispatch a firmware/native build. No hardware/flash/energized
operation occurred. PR remains draft; no external comments/messages are sent.

## CI follow-up on first remediation head

On0f4376c1b045817e3eb78487dbae7a1cb7a46b81, the architecture job122602037419
exposed an author test-fixture error: its mocked fetch test read the real ADC
blob before mocking, although that job only needs the C11 historical source.
The corrected unit test now owns a temporary manifest and fixture bytes and
checks exact fetch arguments, digest verification and unavailable-object failure.
Production pins remain unchanged; their actual original bytes were separately
verified by the empty-object-database probe. The8 tooling tests were rerun and
passed after this correction. This does not substitute fixture bytes for any
production regression baseline. Final-head CI must confirm the corrected suite.

## Behavior delta and remaining gates

A fresh checkout now builds FAKE by default rather than communication/advisory.
Explicit COMMUNICATION/diagnostic and board selector profiles remain available.
The Owner's ignored root sdkconfig is not overwritten by this remediation.
The advisory runtime clock, Fs2..10, host duration and safety checks are unchanged.
New cached led_reboot telemetry explains an unconfirmed async completion and its
fail-closed recovery requirement; it does not infer NACK versus TIMEOUT internally.

The Owner's197134e image17218177... remains valid historical evidence for its
Fs5/Fs10 ten-second successes. It does not verify the new configuration defaults,
collector/tooling, no-callback behavior or rebuilt image. New candidate verification
must use its exact new source/configuration/image identities. Hardware/RTOS must
review the error/recovery delta; non-author must execute native regressions. Any
on-target retest requires a separate approved Owner procedure; sustained/electrical/
thermal/optical qualification is still unestablished. No additional physical
measurements were made a prerequisite for software remediation.

| Remaining gate | Owner |
| --- | --- |
| Required checks and appropriate final-SHA artifacts | CI / Integration; inspect actual final run |
| Native regression execution and failure controls | Assigned non-author verifier, within granted build/link authority |
|800kHz, SHORT policy, reset/bus and electrical boundaries | Hardware Specialist |
|Callback absence, retained lifetime, priority22/ISR/drain/timing/stack | RTOS Specialist |
|Independent review of final candidate | Assigned eligible non-author IR in isolated review context |
|Build authorization, image/bench procedure and final human decision | Owner/Nori with Agent01 as applicable |

## Ready-to-paste handoff and commands awaiting permission

> Author remediation for PR74, continuing issue33. Reviewed baseline5c0fae9;
> implementation remediation9fc3ac7630420ed621245da306e4a15b0eeaf023 plus this
> documentation-only commit. Resolve and record final PR head before verification.
> All seven technical findings have author fixes; no specialist/IR approval is
> claimed. Inspect .vscode/settings.json, sdkconfig.defaults/sdkconfig.fake,
> check_ads131_config.py, both CI workflows, collect_inner_build.py,
> test/host/CMakeLists.txt, run_host_tests.py, workflow fixtures,
> fetch_regression_sources.py/regression_sources.json, and nn2_led_bringup.c/.h
> with test/led/test_led.c. Execute no-callback NACK and TIMEOUT cases in both
> policies; verify pending storage cannot be reclaimed, START/CLEAR_FAULT remain
> denied and reboot-required telemetry is accurate. Check original historical
> regression SHA/hash provenance, old-fail controls, matrix exclusivity and exact
> inner/platform coverage. Hardware and RTOS reviews remain required. Do not merge,
> change Ready state, weaken gates, or operate hardware without separate authority.

Minimum additional local authorization for native LED verification: configure,
compile and link only the two host test executables in a new ignored build folder,
then execute CTest. No firmware, flash, serial/USB/Wi-Fi access or energized test:

```powershell
# Native MSVC developer PowerShell or native GCC/Clang environment, not Xtensa.
cmake -S test/led -B build/ir74-led-host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ir74-led-host --parallel 2
ctest --test-dir build/ir74-led-host --output-on-failure
```

Expected: both led_owner_and_pca and led_short_owner_and_pca pass with zero failures,
including the new two terminal-without-callback cases. These commands were NOT RUN.
Non-author communication loopback coverage additionally needs:

```powershell
cmake -S test/communication -B build/ir74-communication-host -G Ninja
cmake --build build/ir74-communication-host --parallel 2
ctest --test-dir build/ir74-communication-host --output-on-failure
python test/communication/test_host.py --loopback build/ir74-communication-host/nn2_loopback.exe
```

For Linux, omit .exe. Expected: communication_policy passes and all seven Python
loopback cases execute without skip. These commands were NOT RUN locally.
The existing CI also executes historical ADC/C11 regression-first controls and
inner-layer native tests; author local execution remains NOT RUN.

**Owner build required**, for a new advisory image in ESP-IDF6.1 PowerShell:

```powershell
idf.py -B build/ir74-advisory -D SDKCONFIG=build/ir74-advisory/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup;sdkconfig.led-short-bench' -D NN2_EXPECT_LED_POLICY=BENCH_WARN -D NN2_EXPECT_LED_I2C_HZ=800000 -D IDF_TARGET=esp32s3 build
```

For strict regression build, use a separate new path and explicit expectations:

```powershell
idf.py -B build/ir74-strict -D SDKCONFIG=build/ir74-strict/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup;sdkconfig.led-strict' -D NN2_EXPECT_LED_POLICY=STRICT -D NN2_EXPECT_LED_I2C_HZ=400000 -D IDF_TARGET=esp32s3 build
```

Expected: matching generated profile/clock/IRAM assertions and successful build.
No Full Clean, flash or target RUN is requested by these commands. Local firmware
builds remain NOT RUN by agent. Size/map/IRAM and reproducibility evidence must
be collected by an authorized verifier/Owner or appropriate CI, not inferred from
syntax checks or the old ELF. Inspect final CI before requesting redundant builds.
