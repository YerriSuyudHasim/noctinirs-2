# ADR-0004 local implementation verification

Date: 2026-10-27, Asia/Seoul. This is local, provisional evidence; not immutable
CI, an independent review, or hardware acceptance. Execution UTC and exact input
hashes are in `evidence/adr004-2026-10-27.json`.

Base: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1` plus a dirty working tree containing
both pre-existing changes and this implementation. Tests are attached to working
source hashes, not attributed to base HEAD alone. The Owner requested reversible
implementation; no commit/push/merge, firmware build, flash or board operation was
performed. The preserved proposal is PR #55 at
`222f9c45f82b6014db85583924da00e313e5499c`.

## Results

| Class | Check | Result |
|---|---|---|
| Host/unit | Production C codec, registry and control state, plus retained provisioning regression | PASS, 418 checks, zero failures |
| Host/integration on Windows loopback | Independent Python codec and real UDP server built from production C policy | PASS, 13 Python tests; includes 7 actual C-server loopback tests, no skips |
| Host/unit with platform mocks | Production board-power driver and battery schedule | PASS, 37 CTest cases, zero failures |
| Static | Clang 21.1.3 strict C11 syntax for codec, communication application and C tests | PASS, -Wall -Wextra -Werror -pedantic |
| Static with mock IDF headers | Same Clang C11 syntax for board power, battery monitor and power tests | PASS; not real IDF compilation |
| Static | Inner include/CMake gate | PASS |
| Static | Platform-boundary regression | PASS, 5 tests |
| Host/unit | Existing Windows Wi-Fi synthetic fixture parser | PASS, 3 tests; no real credentials queried |
| Static | Reference manifest | PASS, 319 files before/after work |
| Static | git diff --check | PASS; existing CRLF normalization warnings are not compiler warnings |

Communication behavior covered: independent wire fixtures, CRC32 check vector,
maximum 1472-byte envelopes, truncation/corruption/ASCII rejection, valid-CRC
reserved flags and protocol mismatch, registry argument ranges, mutation retries,
same-ID argument/opcode conflicts, public-query cache isolation, IP+port peer
lock, stale-session rejection, explicit restart after silence+grace expiry,
timestamp echo/software TX preparation, fault propagation, USB safety-policy
entrypoint, config preservation, and request-ID wrap including zero.

The seven real loopback tests are control flow, duplicate/conflict/peer behavior,
corruption/truncation rejection, stale-session rejection, expiry/restart, and
CRC-valid reserved-flags/version errors, and lost STOP acknowledgement replay
after session teardown. That final regression first failed against the prior
implementation, then passed after cache lookup was moved ahead of current-session
validation for exact acknowledgements. Old-session acknowledgements cannot
refresh a new session; cached semantic errors cannot refresh owner liveness.
Host loopback does not exercise ESP32
Wi-Fi, lwIP binding, USB transport, ISR behavior or radio latency.

Power coverage includes 23 individual startup-operation fault injections, current
and voltage readback mismatch, preservation of unrelated REG02/REG04 bits, CE
ordering, missing/wrong devices, safety-timer rejection, bus handoff/cleanup errors,
partial measurement rejection and battery cadence. Setpoints: REG02 ICHG=0 with
FORCE_20PCT cleared (512 mA nominal); REG04 VREG=42 (4176 mV nominal). No measured
current, voltage, temperature, SOC accuracy or cell compatibility is claimed.

## Toolchain and commands

Native execution: MSVC 19.50.35721.0, Windows x64, C11, /W4 /WX; CMake 4.0.3;
Ninja 1.12.1; Python 3.13. Final native builds reported no compiler warnings.
The Espressif Clang executable cannot produce Windows x64 binaries, but its
frontend can perform strict native-target syntax checks; those checks passed.
No compiler or dependency was installed or upgraded.

With the installed Visual Studio x64 developer environment and Ninja on PATH:

```powershell
C:/Espressif/tools/cmake/4.0.3/bin/cmake.exe -S test/communication -B build/adr004-host -G Ninja -DCMAKE_C_COMPILER=cl
C:/Espressif/tools/cmake/4.0.3/bin/cmake.exe --build build/adr004-host
build/adr004-host/nn2_comm_tests.exe
python test/communication/test_host.py --loopback build/adr004-host/nn2_loopback.exe
C:/Espressif/tools/cmake/4.0.3/bin/cmake.exe -S test/board_power -B build/board-power-msvc -G Ninja -DCMAKE_C_COMPILER=cl
C:/Espressif/tools/cmake/4.0.3/bin/cmake.exe --build build/board-power-msvc
C:/Espressif/tools/cmake/4.0.3/bin/ctest.exe --test-dir build/board-power-msvc --output-on-failure
python tools/class-s-gate.py
python -m unittest discover -s tests/architecture -p test_platform_boundary.py
python -m unittest discover -s test/communication -p test_windows_wifi.py
```

Strict syntax uses `clang.exe --target=x86_64-pc-windows-msvc -std=c11
-fsyntax-only -Wall -Wextra -Werror -pedantic` with the corresponding Core/Ports/App
include directories (and explicit test mock/board includes for power). This is a
frontend/static check, not an embedded build or GCC/Clang Class U CI attestation.

Local artifacts: `build/adr004-host/final-build.log`, `final-c-tests.log`,
`python-tests.log`; `build/board-power-msvc/adr004-build.log`, `adr004-tests.log`,
`adr004-tests.xml`. Checksums are in the source identity record. Initial native
compiler warnings (a complement cast and a non-lvalue array expression) were
corrected; final strict and native builds pass. The earlier broad architecture
suite had 15 environment/fixture failure reports, recorded in BOARD_POWER_EVIDENCE.
It was not reclassified as passing and has not been rerun as a full suite here.

## Limits before firmware/hardware acceptance

Target remains ESP32-S3 / installed ESP-IDF 6.1.0. Existing main stack setting is
12288 bytes, PSRAM disabled, SoftAP profile selected. Installed I2C and esp_random
headers were inspected; no complete target link/build was run. Actual firmware
warnings, flash/RAM/binary size, main stack watermark and timing are NOT RUN.

The USB SAFE_OFF line parsing and AP socket binding were inspected but not executed
against actual IDF/USB/Wi-Fi. Protocol FRAME envelope encoding is tested; there is
no FRAME producer, real optical stream, SESSION_REPORT emitter or acquisition
completion/recovery implementation. The binding explicitly reports this through
SIMULATED replies and capabilities. Full upstream ADR conformance is not claimed.

Bootloader P7 safe boot is not integrated. RESET_LED begins being asserted by this
local code in app_main; earlier boot behavior remains unverified. Hardware/RTOS
reviews and a separately approved board/charging procedure remain necessary.
On-target bench, HIL, field/production, battery and RF tests: NOT RUN.

Rollback/recovery and remaining protocol decisions are documented in
`adr/0004-local-implementation.md`. Firmware and host client must move together.
