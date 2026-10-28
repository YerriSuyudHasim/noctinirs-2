# LED bring-up local verification record

Date: 2026-10-27; verification completed by approximately 08:07 UTC (17:07 KST).
Scope/acceptance/roles/risks: [ADR-0005](adr/0005-led-bringup.md).
Operator procedure: [LED_BRINGUP.md](LED_BRINGUP.md).

## Source identity and environment

Base HEAD: 5958a0eafec6b03c2b158c4026f3f10b8a3366d1, branch main plus existing
uncommitted/untracked workspace changes. No commit, push, PR write or merge was
performed. This is a local review candidate, not a completed issue/PR/CI/review
workflow. Existing unrelated changes and historical references are preserved.

Active-source SHA256:
93c0646c43e999ef8d3d9e1314730a70e5f1a3662bd9faaf5a7552a1b0b1b0a2

[Source inventory](evidence/led-bringup-2026-10-27.json) covers 161 active
implementation/config/tool/test inputs, including the new bootloader hook and
LED defaults, with per-file hashes and local artifact hashes. Documents and
generated build/sdkconfig are excluded from that aggregate. A base SHA alone
does not identify this firmware. Older evidence records describe earlier sources.
Editable local records do not replace exact-head CI or controlled bench evidence.

Windows native tests: MSVC 19.50.35721.0 x64 C11, /W4 /WX, CMake4.0.3,
Ninja1.12.1; Python3.13.5. Target static frontend: installed ESP-IDF6.1 commit
fff9895c82d744c7237be8847347bdd1b07c6643, Xtensa ESP GCC15.2.0 from
esp-15.2.0_20251204. Target intent ESP32-S3; assembled board revision NOT VERIFIED.

## Results

| Evidence class | Command / scope | Result |
|---|---|---|
| Host/unit, fake GPIO/I2C/timer/RTOS | CMake test/led -> build/led-host, nn2_led_tests.exe | 4496 checks, 0 failures |
| Host/unit, production application/codec | CMake test/communication -> build/led-comm-host, nn2_comm_tests.exe | 534 checks, 0 failures |
| Host/integration, actual C UDP server on localhost | python test/communication/test_host.py --loopback build/led-comm-host/nn2_loopback.exe | 13 tests PASS, including 7 real-C UDP cases; no skips |
| Host/unit, CLI client model only | python -m unittest discover -s test/communication -p test_led_cli.py -v | 5 PASS |
| Host/unit, fake serial only | python -m unittest discover -s test/communication -p test_usb_status.py -v | 5 PASS; no COM port opened |
| Host/unit, power driver and cadence | CMake test/board_power -> build/led-power-host; ctest --test-dir build/led-power-host --output-on-failure | 37/37 PASS |
| Static | python tools/class-s-gate.py . | PASS |
| Host/unit, architecture gate fixtures | python -m unittest discover -s tests/architecture -p test_platform_boundary.py -v | 5 PASS |
| Static target frontend, not firmware build | 7 syntax probes described below | PASS, -Werror, no diagnostics |
| Static reference integrity | tools/verify-reference.ps1 before/after | 319 files PASS |
| Static whitespace | git diff --check | PASS for tracked diff; existing CRLF normalization warnings |
| Target build/link, generated LED configuration, image size | Full idf.py LED/communication builds | NOT RUN by agent |
| On-target bench/HIL | Flash, reset/light/current/thermal/cable/phase measurements | NOT RUN by agent |
| Independent/specialist review | Hardware, RTOS, nonauthor review on candidate | PENDING |

Native compilation initially identified signed ESP error storage and temporary
structure-array access warnings under /WX; these were corrected. Final native
builds have no such diagnostics. No check threshold was weakened.

## What the tests establish

LED tests execute the production register adapter and bounded owner step/ISR
callback with deterministic fake peripherals. They cover all byte-valued Fs
inputs, the exact allowed set and timing geometry; all 20 mappings and unused
channels; one-hot ON/all-off transitions; IREF=133 for 30 mA; readback mismatch;
each initialization I2C failure; current readback corruption; overtemperature
health; late/coalesced phases; transfer overrun; cancellation before/during
prepare and active work; timer start/alarm errors; occupied controller; retained
handles/clear-fault refusal on cleanup failure; each of 38 boot-safe-off I2C
failure points. Fake critical sections are not a multicore RTOS model; these
tests do not prove all physical interleavings, IRQ masking, IRAM placement or WCET.

Application tests add default5/current30; immutable active configuration;
STARTING/STOPPING completion; backend fault propagation; real-mode current cap;
clear-fault while cleanup remains pending; STOP acknowledgement replay after
asynchronous session teardown. Queries cannot drive LEDs. Cache replay checks
the acknowledged session before refreshing activity. Native loopback tests
remain disabled-emitter control tests, not tests of the ESP LED task.

CLI models verify explicit --led capability matching, optional --rate configured
before START, >10 rejection and waiting for stop completion after session release.
Actual serial no-reset behavior was Owner-reported on the preceding host fix;
it was not retested physically by the agent for this candidate.

## Target syntax method and limits

Used existing generated build/compile_commands.json and bootloader equivalent to
recover target compiler/include/define settings. Removed -c/output/dependency
generation, used -fsyntax-only -Werror and the exact current source. Added the
new driver public includes and their installed transitive HAL/VFS includes,
which the newly declared CMake requirements must supply on a fresh build.
For LED probes injected the four intended CONFIG flags at the command line;
this does not establish that generated Kconfig or the linker will honor them.

Probes: nn2_board.c, nn2_board_pca.c, app_main.c default and LED-enabled,
nn2_led_bringup.c, bootloader hook using bootloader flags and nn2_provision.c.
Commands/results retained in ignored build/led-syntax-results.json, hashed in
the source inventory. Initial missing-include errors were corrected in this
ad-hoc probe harness; no SDK files or generated configurations were modified.
All seven final frontend invocations exit zero. This emits no application or
bootloader image and establishes no actual hook linkage, IRAM map, size or runtime.

## Pending before board acceptance

Owner full build/size/map/configuration evidence; Hardware/RTOS/independent review;
specific approved flash/energization procedure; assembled board and cable map;
current/thermal/optical limits and measurements; ROM-to-hook reset trace; phase
latency/jitter and fail-off traces; stack/heap/watchdog measurements. No target
timing or electrical correctness claim can be derived from the fake clock.
The 100 kHz implementation does not meet or claim the future provisional 250 us
acquisition commit budget. Full ADR-0003 topology and ADC/FRAME work remain absent.

Rollback uses a matched reviewed source/config/image/client and bootloader
recovery plan. Retaining the default disabled LED flag prevents ordinary START
from driving emitters. Turning the flag off does not replace a flashed image or
guarantee a physical off state. Do not alter charger registers, erase NVS or touch
security/eFuses as an automatic recovery step.
