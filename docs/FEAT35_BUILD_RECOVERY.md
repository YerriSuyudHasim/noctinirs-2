# feat35 Windows build recovery, 2026-10-28

Scope: investigate the reported SDK compiler crash and publish the architecture
study for follow-up. No firmware behavior, historical source, SDK, pin, clock,
optimization or toolchain changes. Accountable role: Firmware Developer;
architecture proposals require Architect/Hardware/RTOS and independent review.

## Finding

The reported failure was an internal GCC segmentation fault during RTL `ira` in
ESP-IDF's `esp_lcd_panel_rgb.c:rgb_panel_draw_bitmap`, not an application diagnostic.
At source commit `31fb2a9101fbb12a59e52506464d476e184bbdd9`, the user's GitHub
checkout had no unmerged entries, no pending merge and no difference in
`main/app_main.c`. The SDK RGB source was unchanged. There is no evidence that
the aborted merge damaged app_main. The same failed object compiled successfully
with one job, without source changes, followed by a successful full build with
three jobs. The owner independently reported that retrying Build succeeded.

The crash did not reproduce. Its exact cause is unknown; memory pressure or
parallelism has not been established as the cause. Workspace settings now bound
both the ESP-IDF extension's Ninja task (`idf.ninjaArgs`) and terminal idf.py
(`IDF_PY_BUILD_JOBS`) to three jobs. This is resource control, not a proven GCC fix.
The installed ESP-IDF extension 2.3.0 declares `idf.ninjaArgs` in its schema.
Remove those two settings to restore default parallelism.

## Evidence (local compilation, not hardware evidence)

- ESP-IDF v6.1, SDK commit `fff9895c82d744c7237be8847347bdd1b07c6643`;
  bundled Xtensa GCC 15.2.0 (`esp-15.2.0_20251204`), target ESP32-S3.
- Failed object retry: `ninja -C build -j 1 esp-idf/esp_lcd/CMakeFiles/__idf_esp_lcd.dir/rgb/esp_lcd_panel_rgb.c.obj`, exit 0.
- Full build: `IDF_PY_BUILD_JOBS=3`, `idf.py -B build build`, exit 0,
  including bootloader, application link and binary size checks.
- Application image: 839408 bytes; image SHA256
  `a863519a136c405185298ee3409a913e7d60c2656a963d2c8c6502b67fcc8cf4`.
- Generated sdkconfig SHA256
  `39149241f2910f6b5883eb33fa6e297ae754af983eba86da0cdd88775c38d91a`.
- Recovery log: `C:/esp/feat35-local-build-recovery.log`; object retry log:
  `C:/esp/feat35-rgb-retry.log`. No warning diagnostics in the recovery log;
  it is an incremental recovery build, not a clean rebuild of every SDK source.

This local configuration enables communication and LED short-bench mode at
800 kHz, uses a 2 MB flash image, and **does not enable optical acquisition**.
Building a feat35 branch does not automatically enable its optional ADC binding.
Existing acquisition build evidence is in `ADS131_ACQUISITION_RESULTS.md` and
`evidence/ads131-acquisition-2026-10-28.json`; it is separate evidence from this
LED-only recovery. The acquisition configuration overlay is `sdkconfig.acquisition`
on top of defaults, communication, LED bring-up and LED short-bench overlays.
Use a separate build directory and generated sdkconfig when selecting it; existing
sdkconfig values override defaults. Do not interpret this successful LED build as
ADC/LED integration qualification or change flash geometry merely to clear a build.

Acceptance: reported object and full local configuration build successfully;
reference integrity remains unchanged; discussion findings are available for the
follow-up agent. No runtime tests repeated because firmware behavior did not change.
No flash, board operation, merge or release. If the ICE returns, retain the exact
compiler invocation and preprocessed source before changing source or toolchain.
