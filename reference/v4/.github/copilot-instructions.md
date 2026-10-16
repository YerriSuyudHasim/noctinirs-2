# Repository Instructions

## Project
- This is ESP-IDF firmware for the ESP32-S3. `CMakeLists.txt` enables `MINIMAL_BUILD`; keep dependencies explicit in component `CMakeLists.txt` files.
- `main/app_main.c` composes the `nocti_v4_*` components. Put subsystem behavior in its owning component rather than growing the application entry point.
- The active project is V4-7.2. The `patches/v4-7-2-idf-5-5-5` tree is an isolated, unbuilt migration candidate; do not treat it as authoritative for the root project.

## Real-Time Constraints
- Preserve the required app-main CPU 1 affinity, Wi-Fi CPU 0 pinning, and `CONFIG_I2C_ISR_IRAM_SAFE`; `app_main.c` intentionally fails compilation if these settings are absent.
- Preserve ISR/callback cache-safety and task ownership when changing I2C, LED, scheduler, or acquisition paths. Follow existing component boundaries and nonblocking handoff patterns.
- Treat packet formats, optical timing, safety-off behavior, and acquisition semantics as externally significant contracts.

## Build And Validation
- The original verified baseline is ESP-IDF 5.5.1. For the requested migration, build the root project with ESP-IDF 6.1, targeting `esp32s3`, using `idf.py build`.
- A successful build establishes compile/link compatibility only; preserve 5.5.1 as the comparison baseline until required hardware validation is complete.
- `tools/build_clean.ps1` currently embeds another machine's ESP-IDF and tool paths; inspect and adapt it for the local machine before using it.
- There are no checked-in test files. Use a successful build as the minimum software check for firmware changes; hardware behavior still requires hardware validation.
- Do not flash or monitor hardware unless the user asks. Keep generated build output and local `sdkconfig` changes out of source edits unless the task specifically requires them.

## Change Practices
- Keep changes scoped to the owning component and update its declared dependencies when needed.
- Preserve the root project as the authoritative implementation; make changes to the migration candidate only when explicitly requested.
- Avoid changing real-time settings, protocol ABI, or scientific behavior without tracing their callers and validating the relevant contract.