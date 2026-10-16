# NoctiNIRSv3 Changelog

## 3.4.6 — P5_ADS_SYNC_STATUS_FIX

- Fixed a false startup rejection where the first STATUS response after an intentional SYNC pulse set `F_RESYNC`.
- Split `F_RESYNC` from hard ADS status faults; only one indication is accepted inside the bounded post-SYNC window.
- Added three post-SYNC discard frames before startup rate measurement and before runtime sample delivery, preserving sinc3 settling and FIFO realignment.
- Kept LOCK, REG_MAP, CRC_ERR, RESET, CRC type, word length, output CRC, SPI errors, and continuity checks strict.
- I2C/PCA9956B V2 bus port remains unchanged; the startup log already confirms synchronous validation and asynchronous runtime readiness.

# 3.4.5 — Port validated V2 I2C/SPI bus behavior

- Retained the existing NoctiBoard ADS131 GPIO35-38 map and explicit software PSRAM disable.
- Ported the V2 I2C phase contract: qdepth=0 synchronous PCA reset/config/readback, followed by destroy/recreate as qdepth=4 asynchronous runtime.
- Removed the invalid assumption that unregistering a callback converts an asynchronous ESP-IDF bus into a synchronous bus.
- Preserved persistent runtime LEDOUT payloads and callback-derived completion timestamps.
- Matched ADS131 SPI bus/device configuration to validated V2: mode 1, 8 MHz, DMA disabled, one contiguous 18-byte polling transaction, max transfer 256 bytes, CS pre/post guard 1/1.
- Removed the obsolete GPIO35-37 hardware warning from boot and replaced it with an explicit V2-pin-map/PSRAM-disabled status line.

# 3.4.4 — Native USB Serial/JTAG console fix

- Changed the primary ESP-IDF application console from UART0 to the ESP32-S3 native USB Serial/JTAG controller.
- Targets the custom WROOM board connection GPIO19 = USB D- and GPIO20 = USB D+ without CH340/CP2102.
- Removed UART0/115200 compile-time assumptions from application and UDP `LOGSTATUS` diagnostics.
- Added build-fatal validation that `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` and no secondary console are selected.
- Updated generated `sdkconfig`, `sdkconfig.defaults`, host stubs, and logging configuration checker.
- Set project/app version to 3.4.4 so the boot log can confirm that the corrected image is running.

# 3.4.3 — V2/V3 logging parity and hard diagnostics

- Audited actual V2(16) and V3.4.2 logging paths; V2 quiet mode is acquisition-only and cannot hide STOPPED boot/Wi-Fi logs.
- Removed the V3 quiet helper and quiet state completely; no unconditional `printf()` is used.
- Added build-fatal checks for INFO compile ceiling, dynamic/master control, UART0, 115200 baud, and no secondary console.
- Enabled master log-level control and restores master, wildcard/default, project, Wi-Fi, PHY, and netif tags to INFO at `app_main()` entry.
- Added file-local INFO ceilings to every project translation unit that owns INFO diagnostics.
- Expanded staged startup diagnostics and Wi-Fi/UDP logs to parity with and beyond V2.
- Added UDP `LOGSTATUS` to verify compiled/runtime log levels and console routing independently of serial output.
- Added `tools/check_logging_config.py` and `docs/V3_V2_V3_LOGGING_COMPARISON.md`.
- Preserved the no-log rule for ISR, GPTimer callback, async I2C callback, per-sample, and normal per-slot paths.

# 3.4.2 — Serial quiet disabled explicitly

- Removed the unconditional `printf()`/`fflush()` serial probe added in 3.4.1.
- Added an explicit V3 runtime serial policy with `NOCTI_V3_RUNTIME_SERIAL_QUIET=0`.
- `app_main()` now calls `runtime_serial_set_quiet(false)` and resets the wildcard runtime log filter to `ESP_LOG_INFO`.
- A future accidental request for quiet mode is ignored while the compile-time policy remains disabled.
- Kept all ISR, GPTimer callback, ADC per-sample, and normal per-slot paths free of serial logging.

# 3.4.1 — Serial diagnostics and SPI acquire fix

- Fixed ESP-IDF 5.5.1 SPI bus acquisition by using the only supported wait value, `portMAX_DELAY`.
- Restored/forced INFO visibility for all NoctiNIRSv3 project tags at startup.
- Added unconditional `app_main` stdio console probe and clearer Wi-Fi/health state fields.
- Set flash image header to 16 MB for ESP32-S3-WROOM-1-N16R8.
- Kept logging outside ISR, timer callback, ADC sample path, and normal per-slot path.

# CHANGELOG — NoctiNIRSv3

## 3.4.0 — P5 Final Integration — 2026-07-28

### Added

- Dedicated non-real-time UDP stream task on core 0.
- Explicit completed-frame ownership transfer from optical processing to UDP.
- Nonblocking `sendto(..., MSG_DONTWAIT, ...)` and completed-frame drop policy.
- Separate Wi-Fi link, network-ready, peer-active, stream-enabled, acquisition,
  transmit-time, command-time, and sensor-fault status.
- Backward-compatible stream-v1 encoder and host decoder/tests.
- SESSION v1, TIME64, FNIRS_CFG v2, and paired FNIRS_FRAME v2 records.
- Final command set including stopped-only current profile updates.
- Dedicated low-priority health logger and application task core/stack reporting.
- Kconfig choice for default 400 kHz and experimental 1 MHz I2C profiles.
- Final task-priority compile-time assertions.
- Bounded acknowledgement-based network-task cleanup on partial initialization.
- Host acceptance runner, control client, UDP capture tool, and protocol parser.
- Final architecture, packet, command, health, fault, and validation documents.

### Changed

- UDP networking no longer participates in ADC, scheduler, LED, or optical
  processing execution paths.
- The UDP stream task is the sole completed-frame consumer; it copies each frame
  into persistent task-owned storage before transmission.
- No-peer or slow-network conditions consume/drop completed network frames and
  increment `network_drop`; acquisition continues.
- Health logging moved out of optical/ADC tasks into priority-5 health task.
- `sdkconfig.defaults` consolidated for CPU performance, dual-core operation,
  core affinity, watchdogs, IRAM-safe callback paths, logging, BLE disablement,
  and hot-data/PSRAM constraints.
- Root README updated to distinguish source/static/build/hardware/acceptance gates.

### Compatibility

- Stream-v1 12-byte envelope, record identifiers, CRC16 behavior, and v16 fNIRS
  payload layouts are retained.
- Existing text commands remain recognized where they were part of the fNIRS
  validation path.
- No MBLL, SSR, baseline, bandpass, or UI redesign was introduced.

### Not verified in this environment

- ESP-IDF 5.5.1 target compile/link.
- Linker-map IRAM placement.
- GPIO35–37 compatibility with the exact N16R8 module.
- Hardware timing, optical edge timing, 8 kSPS integrity, K=10/10, stack margins,
  Wi-Fi stress, I2C recovery, 100-cycle START/STOP, or long soak acceptance.

## 3.3.0 — P4 Optical Processing

- Added timestamp classification, latest-contiguous-K averaging, slot validity,
  20-slot frame assembly, wavelength pairing, `I_corrected`, completed-frame
  ring ownership, health counters, and final session/config epoch filtering.

## 3.2.0 — P3 LED Scheduler

- Added nonblocking PCA9956B command path, I2C completion callback, persistent
  payloads, absolute GPTimer sub-deadlines, slot state machine, and recovery task.

## 3.1.0 — P2 ADC and Timebase

- Added common GPTimer timebase, minimal DRDY ISR, ADS task, 18-byte SPI frame,
  CRC/status validation, raw SPSC ring, DARK mode, and rate validation.

## 3.0.0 — P1 Bootstrap

- Created clean project tree, architecture contract, board fail-safe bootstrap,
  baseline Wi-Fi/UDP command transport, and migration map.
