# NoctiNIRSv3 Final Build Notes

## Required environment

```text
ESP-IDF: 5.5.1 exactly
Target:  esp32s3
Toolchain: matching Xtensa ESP32-S3 GCC/tools
```

```bash
. "$IDF_PATH/export.sh"
idf.py --version
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Archive the generated sdkconfig, full build log, map, size report, and binary
hash. Review warnings rather than assuming defaults guarantee correctness.

## Final sdkconfig defaults and rationale

| Setting | Reason | Risk/review |
|---|---|---|
| performance optimization, CPU 240 MHz | maximize timing margin | power consumption increases |
| 1000 Hz FreeRTOS tick, dual core | prior architecture and millisecond control timeouts | tick ISR overhead; measure |
| main/Wi-Fi/lwIP on core 0 | isolate source-controlled RT domain on core 1 | verify generated sdkconfig/system tasks |
| interrupt/task watchdogs enabled | detect starvation/deadlock | recovery paths must not monopolize core |
| GPTimer control/ISR cache-safe options | absolute deadline callback safety | verify linker map/IRAM use |
| I2C ISR IRAM-safe | async completion needed under cache-disabled periods | IRAM/DRAM pressure |
| SPI master ISR in IRAM | driver robustness for ADS path | whole driver not forced into IRAM |
| BLE disabled | outside scope | none for fNIRS build |
| info logs, dynamic level | periodic diagnostics without hot-path logs | production level may be reduced after validation |
| PSRAM disabled in software | keep hot data internal and flag GPIO conflict | does not solve physical N16R8 wiring |
| I2C 400 kHz default | conservative validation profile | 1 MHz requires separate evidence |

## Build status in generation environment

See `V3_BUILD_ATTEMPT_P5.txt`. The environment does not provide `idf.py`,
`IDF_PATH`, or the Xtensa toolchain, so no target-build claim is made.

## Host review scope

Narrow ESP-IDF stubs check application syntax, types, format strings, static
assertions, and internal interfaces. They do not prove exact IDF headers,
component linking, IRAM placement, flash size, boot, or peripheral behavior.

## Timing calculations to verify on target

```text
ADS frame: 18 bytes = 144 bits / 8 MHz = 18 us ideal wire time
PCA LEDOUT: 72 SCL clocks = 180 us at 400 kHz; 72 us at 1 MHz
ON design window: 2500 - 250 allowance - 500 settle = 1750 us
OFF design window: 2500 - 250 allowance - 500 settle - 400 guard = 1350 us
```

These are design calculations, not guaranteed sample counts. Measure completion
latency, DRDY phase, scheduler lateness, and actual stable windows.


## 3.4.1 serial/SPI correction

- Root cause of runtime SPI error: `spi_device_acquire_bus()` was called with `pdMS_TO_TICKS(3000)`. ESP-IDF 5.5.1 only supports `portMAX_DELAY` for this API.
- Corrected both startup-validation and session-start call sites through a no-timeout wrapper that always uses `portMAX_DELAY`. SPI3 remains dedicated to the ADS task and is released on STOP.
- Project log tags are explicitly set to INFO at `app_main` entry. Wi-Fi connection/DHCP events and 5-second health lines expose AP/station/peer/stream state without adding hot-path logging.
- Flash image size changed from 2 MB to 16 MB for the N16 module.
- Container validation: `tools/host_static_check.sh` passed 11/11 translation units; `tools/run_host_logic_tests.sh` passed stream-v1 CRC/layout/sequence.
- ESP-IDF 5.5.1 cross-build was not run in this environment because the ESP-IDF toolchain is unavailable. A clean hardware build/flash is still required.


## 3.4.2 serial-unquiet correction

- The 3.4.1 unconditional `printf()` probe was removed as requested.
- V2 used `FNIRS_RUNTIME_SERIAL_QUIET=1` and called `runtime_serial_set_quiet(true)` before START. V3.4.2 makes the opposite policy explicit: `NOCTI_V3_RUNTIME_SERIAL_QUIET=0` and `runtime_serial_set_quiet(false)`.
- The false path sets the wildcard runtime log level (`"*"`) to INFO. This is stronger than restoring only named V3 tags and is intended to recover INFO output when a prior wildcard/runtime filter is at ERROR.
- This does not add logging to DRDY ISR, GPTimer callback, asynchronous I2C callback, ADS sample processing, or normal slot transitions.
- Clean rebuild is required because `main/app_main.c` and the firmware version changed.


## 3.4.3 V2/V3 logging audit and hard diagnostics

- Source audit result: V2(16) contains 164 `.c` log calls and 21 Wi-Fi log calls; V3.4.2 contained 36 and 7 respectively. V3.4.3 contains 84 and 39.
- V2 quiet mode is entered only around START/acquisition and restored on failure/STOP. It is not a credible cause of absent STOPPED boot or Wi-Fi logs.
- V3.4.3 removes the quiet helper entirely and has no `printf()` probe.
- Build is rejected when INFO is compiled out, dynamic/master control is disabled, or the application console is not UART0 at 115200 with no secondary console.
- `CONFIG_LOG_MASTER_LEVEL=y` is enabled. `app_main()` restores master/default/named tags to INFO. Project files with INFO diagnostics define `LOG_LOCAL_LEVEL ESP_LOG_INFO`.
- Wi-Fi initialization now logs NVS, netif, event-loop, AP-netif, driver, storage, handlers, config, start, IP/GW/netmask, station/DHCP events, UDP sockets/binds, tasks, and final readiness.
- UDP command `LOGSTATUS` independently reports build/runtime levels and console route.
- `python tools/check_logging_config.py sdkconfig`: PASS.
- Host static review: 11/11 translation units PASS. Stream-v1 CRC/layout/sequence: PASS.
- ESP-IDF 5.5.1 target compile/link and hardware serial verification remain pending because this environment has no `idf.py` or Xtensa toolchain.

## 3.4.6

Source revision `P5_ADS_SYNC_STATUS_FIX`. Host static syntax checks are required in this package. Full ESP-IDF 5.5.1 build was not available in the packaging environment and must be run by the user.
