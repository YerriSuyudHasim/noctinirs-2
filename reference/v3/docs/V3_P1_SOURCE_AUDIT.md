# NoctiNIRSv3 Prompt 1 Source Re-Audit

## Scope reviewed

The following v16 source was re-read before creating v3:

```text
main/app_main_fnirs_test.c
components/nocti_timebase/*
components/nocti_scheduler/*
components/nocti_fnirs/*
components/nocti_hal_spi/*
components/nocti_hal_i2c/*
components/nocti_led_driver/*
components/nocti_board/*
components/nocti_stream/*
components/nocti_wifi/*
components/nocti_common/*
root/main/component CMakeLists.txt
sdkconfig, sdkconfig.defaults
```

The prior audit report, ADS131M04/PCA9956B/ESP32-S3 references, and TI sample driver sources were used as design evidence. TI `hal.c` is MCU-specific example code and was not copied into ESP-IDF.

## Current v16 execution chain confirmed

```text
GPTimer slot-start ISR
  -> scheduler task
     -> synchronous-wait LED write
     -> software wait to LED-OFF
     -> synchronous-wait LED write
     -> software wait to slot end
     -> processing queue

ADS DRDY ISR
  -> timestamp/sequence event
  -> ADS task
     -> one 18-byte SPI polling frame
     -> application history buffer

processing task
  -> timestamp search/average/pairing
  -> stream ring
  -> UDP TX task
```

## Prompt 1 findings that directly shaped v3

| Finding confirmed from v16 | P1 correction |
|---|---|
| timing constants spread through `app_main_fnirs_test.c` | all constants moved to `nocti_v3_config.h` |
| `async_write_wait` submits then waits on completion | not migrated; final API must be submit/completion event |
| scheduler invokes LED transaction and recovery in near-real-time path | no scheduler/LED runtime path exists in P1; boundaries reserved |
| v16 board initially drives PCA RESET# high | v3 asserts RESET# low before bus initialization |
| GPIO ISR service/interrupt allocation core was not an explicit P1 owner | P1 leaves ISR service uninstalled; Prompt 2 installs it from the selected real-time core |
| v16 stream-v1 header is consumed by the bridge | v3 freezes magic/version/record IDs/12-byte envelope while binary TX is deferred |
| raw ADC history lacks explicit consumer tail/overflow | not migrated; Prompt 2 requires static SPSC head/tail |
| START/STOP uses fixed delays | P1 rejects START and implements idempotent safe STOP; final contract requires acknowledgements |
| Wi-Fi owns parts of START/STOP/streaming state | v3 Wi-Fi owns only transport; command parser calls optical owner |
| lwIP affinity was no-affinity in current v16 config | v3 defaults request core-0 affinity |
| v16 SPI read is one 18-byte polling transaction at 8 MHz | retained as Prompt 2 design decision |
| v16 command/ports are used by liveview bridge | ports and text commands remain recognized |

## Datasheet-derived constraints retained

- ADS131M04 is a four-channel simultaneous 24-bit ADC and supports the required 8 kSPS operating point.
- ADS131M04 uses an active-low DRDY and SPI mode 1 in the validated implementation.
- PCA9956B supports 24 outputs, per-output current control, active-low RESET/OE, and Fm+ capability.
- PCA9956B output change can be configured for ACK or STOP; final v3 runtime will explicitly verify change-on-STOP before SCAN.
- ESP32-S3 provides dual LX7 cores, SPI2/SPI3, I2C, interrupt matrix, general-purpose timers, and Wi-Fi required by this architecture.

## P1 build tree created

```text
NoctiNIRSv3/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
├── main/
│   ├── CMakeLists.txt
│   └── app_main.c
├── components/
│   ├── nocti_v3_common/
│   ├── nocti_v3_board/
│   ├── nocti_v3_timebase/
│   ├── nocti_v3_adc/
│   ├── nocti_v3_led/
│   ├── nocti_v3_optical/
│   ├── nocti_v3_stream/
│   ├── nocti_v3_wifi/
│   └── nocti_v3_health/
└── docs/
    ├── V3_ARCHITECTURE_CONTRACT.md
    ├── V3_MIGRATION_MAP.md
    ├── V3_PROGRESS.md
    ├── V3_BUILD_NOTES.md
    └── V3_P1_SOURCE_AUDIT.md
```

## Remaining audit risks

- The new source still requires compilation against the exact ESP-IDF 5.5.1 headers.
- P1 does not prove PCA I2C communication because RESET# is deliberately held low.
- P1 does not prove ADC timing, DRDY rate, or SPI integrity.
- P1 does not prove Wi-Fi does not interfere with future core-1 acquisition; that requires Prompt 2 hardware stress tests.
- The 250 us I2C commit allowance is a design placeholder supported by nominal 400 kHz wire-time analysis, not measured hardware evidence.
