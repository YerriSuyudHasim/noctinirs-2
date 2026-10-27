> HISTORICAL SOURCE RECORD: subsequent ADR-0004 adoption and 512 mA / 4176 mV
> settings change the production source. See ADR004_EVIDENCE.md for newer results.
> Hashes/counts below remain evidence of their own prior revision only.

# Board power verification record

Date: 2026-10-27 (Asia/Seoul); execution UTC and file identities are captured in
`evidence/board-power-source.json`.

Base commit: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1` plus an uncommitted working
tree, including pre-existing Owner changes. Results below apply to the working
source hashes, **not to the base commit alone**. No commit, independent reviewer
attestation, final immutable CI artifact or human merge approval was created.

## Host/unit

PASS: 28 CTest cases compiling the production board power driver and battery
monitor against limited GPIO/I2C/task mocks. Includes 17 boot-operation failure
injections, normal lifecycle, disabled boot, missing/wrong IC, safety-timer
rejection, readback mismatch, partial telemetry failure, existing LED bus refusal,
wrong-task rejection, cleanup failure and polling cadence/freshness behavior.

Native toolchain: MSVC 19.50.35721.0, Windows x64, C11 mode, `/W4 /WX`; CMake
4.0.3 and Ninja 1.12.1. Final host build emitted no warnings. This is **not** an
ESP-IDF firmware build or the repository's GCC/Clang Class U CI attestation.

Commands (with a pre-existing VS x64 developer environment and Ninja on PATH):

```powershell
C:/Espressif/tools/cmake/4.0.3/bin/cmake.exe -S test/board_power -B build/board-power-msvc -G Ninja -DCMAKE_C_COMPILER=cl
C:/Espressif/tools/cmake/4.0.3/bin/cmake.exe --build build/board-power-msvc
C:/Espressif/tools/cmake/4.0.3/bin/ctest.exe --test-dir build/board-power-msvc --output-on-failure --output-junit board-power-tests.xml
```

Ignored local artifacts: `build/board-power-msvc/power-build.log`,
`power-tests.log`, `board-power-tests.xml`, and `Testing/Temporary/LastTest.log`.
Their hashes are in the source identity record; editable local artifacts are
provisional evidence, not immutable CI records.

Initial host build attempts were unsuccessful: Espressif Clang 21.1.3 supports
embedded targets but could not generate Windows x64 code. MSVC initially rejected
`max_align_t`, which its C headers do not supply. The new monitor already uses
memcpy rather than in-place casts, so its opaque storage was simplified to a byte
struct without an unnecessary alignment requirement. Final tests use that source.
These failed attempts are not represented as passing builds.

## Static

- PASS: `python tools/class-s-gate.py` (inner include/CMake dependency audit).
- PASS: `python -m unittest discover -s tests/architecture -p test_platform_boundary.py`
  (5 tests; rejects core-to-board dependencies).
- PASS: `git diff --check` (tracked changes; existing CRLF normalization warnings).
- PASS: `tools/verify-reference.ps1` before and after implementation (319 files).
- Inspected installed SDK version file: ESP-IDF 6.1.0. Inspected I2C API fields,
  bus allocation/cleanup, synchronous transfer selection and SDK recovery paths
  against the installed source; exact SDK Git revision not recorded.

Full architecture suite was also attempted:
`python -m unittest discover -s tests/architecture -p test_*.py`.
Result: **FAIL**, 52 test methods, 15 failure reports (including subtests):

- 10 C11 tests require a GCC/Clang executable on PATH; none was discoverable in
  that shell. The native mock build uses MSVC separately, not a gate bypass.
- 3 shell/CI subtests fail with Windows quoting/CRLF execution errors.
- 2 workflow fixture subtests omit directories required by the pre-existing
  local platform-component manifest (`components/nn2_board` etc.).

No architecture gate, workflow fixture, compiler requirement or shell script was
weakened to turn these failures green. Full CI remains unresolved. These results
do not constitute an independent review or a claim that all repository tests pass.

## Not run and remaining evidence

ESP-IDF target compilation, target compiler warnings, binary/flash/RAM size,
stack watermark and hardware power/latency/memory measurements: **NOT RUN**.
ESP32-S3 is the intended target; physical board revision/configuration/fixture are
not verified. Actual current/voltage limits and battery compatibility have not
been accepted by this source inspection. The existing sdkconfig/build cache was
not regenerated as firmware; host build outputs are isolated under build/.

Simulation, on-target bench, HIL/integration and field/production: **NOT RUN**.
No device was flashed, no battery/emitters/RF were energized by this task, and no
electrical, charge-safety or acquisition-timing claim follows from mock tests.
Hardware, RTOS and independent reviews remain pending before hardware integration.
