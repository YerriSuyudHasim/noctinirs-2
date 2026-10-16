# NoctiNIRSv3 — Final Integration Source (v3.4.0)

NoctiNIRSv3 is a clean ESP-IDF 5.5.1 reconstruction of the NoctiNIRS fNIRS
firmware. The final source integrates acquisition, optical scheduling,
timestamp-based optical processing, backward-compatible stream-v1 UDP output,
non-real-time commands, health reporting, and validation tooling.

## Validation status

| Gate | Status |
|---|---|
| Source implementation | Complete for Prompt 1–5 scope |
| Host static review | Verified, 11/11 translation units |
| Host packet/logic tests | Verified |
| ESP-IDF 5.5.1 target build | Not verified in the generation environment |
| Hardware test | Not verified; no NoctiBoard or hardware logs were available |
| Acceptance matrix | Not verified; procedures and tooling are supplied |

A successful build alone is not acceptance. Timing, K=10/10 behavior, GPIO/module
compatibility, I2C waveform quality, stack margins, and long-run integrity still
require board-level evidence.

## Architecture

```text
DRDY ISR
  -> ADS acquisition task
  -> static raw-sample SPSC ring
  -> optical processing task
  -> static completed-frame SPSC ring
  -> nonblocking UDP stream task
```

The GPTimer, DRDY ISR, ADS task, scheduler, LED-control task, and I2C completion
callback form the real-time domain. Wi-Fi, lwIP, commands, streaming, and health
logging are pinned to the non-real-time core where source and sdkconfig allow it.

## Key contracts

- ESP32-S3 dual core, ESP-IDF 5.5.1, no Arduino API, BLE disabled.
- ADS131M04: SPI3, mode 1, 8 MHz, 24-bit, 8 kSPS target.
- Optical timing: 20 slots × 5 ms = 100 ms frame = 10 Hz.
- K target 10/10; minimum-valid K 8/8.
- PCA9956B: change-on-STOP atomic LEDOUT updates, external pull-ups, hardware
  RESET# fail-safe.
- I2C 400 kHz remains the default. 1 MHz is an explicit experimental profile.
- Stable windows use half-open timestamp intervals and ISR DRDY timestamps.
- `I_corrected = I_LED_ON - I_LED_OFF`.
- MBLL, SSR, baseline, bandpass, and UI remain host-side/out of the hard-real-time
  path.
- No application heap allocation is performed after START; application rings,
  frames, commands, and async I2C buffers are static/persistent.

## Build

```bash
. "$IDF_PATH/export.sh"
idf.py --version          # must report ESP-IDF v5.5.1
idf.py set-target esp32s3
idf.py fullclean
idf.py build
```

Review the resulting `sdkconfig` and linker map; defaults do not replace review.

## Host-side checks

```bash
./tools/host_static_check.sh
./tools/run_host_logic_tests.sh
python -m py_compile tools/nocti_*.py
```

## Validation tools

```bash
python tools/nocti_control.py <device-ip> STATUS
python tools/nocti_udp_capture.py --bind 0.0.0.0 --port 3334 --duration 60
python tools/nocti_acceptance_runner.py <device-ip> dark --duration 600
python tools/nocti_acceptance_runner.py <device-ip> scan --duration 600
python tools/nocti_acceptance_runner.py <device-ip> startstop --cycles 100
python tools/nocti_acceptance_runner.py <device-ip> stress --duration 600
python tools/nocti_acceptance_runner.py <device-ip> soak --duration 3600
```

Manual hardware work remains necessary for true Wi-Fi-off testing, I2C fault
injection, logic-analyzer comparison of 400 kHz versus 1 MHz, and optical edge
validation.

## Critical hardware gate

The retained board mapping uses ADS DRDY/SCLK/MISO on GPIO35/36/37. Confirm the
exact ESP32-S3 N16R8 module and NoctiBoard schematic do not physically reserve
these pins for Octal PSRAM before flashing. Disabling PSRAM in software does not
remove a physical package/module connection.

See:

- `docs/V3_ARCHITECTURE_CONTRACT.md`
- `docs/V3_FINAL_TASK_CORE_AUDIT.md`
- `docs/V3_PACKET_COMPATIBILITY.md`
- `docs/V3_COMMAND_COMPATIBILITY.md`
- `docs/V3_ACCEPTANCE_TEST_MATRIX.md`
- `NoctiNIRSv3_Validation_Checklist.md`
