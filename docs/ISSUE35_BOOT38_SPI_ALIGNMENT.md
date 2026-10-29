# Issue 35 boot 38: first SPI response alignment

Date: 2026-10-29 KST. Baseline commit:
`79ebaea934dfb76c04975d9a10d6fce0117c8979` plus the retained startup diagnostic
working-tree patch. This follow-up supersedes the effective-CS interpretation in
`ISSUE35_STARTUP_INTEGRITY_INVESTIGATION.md`; that earlier report remains a dated
record. Accountable activity: firmware implementation/author tests. Hardware,
RTOS, and independent non-author review are still required. No approval is claimed.

## Reported board evidence and local reproduction

The owner supplied console output after flashing the diagnostic candidate:

```text
boot=38 state=6 fault=267 reset=11
source=79ebaea934dfb76c04975d9a10d6fce0117c8979
image=16699b461dc656b4725f508ed3c9db46bd178f22da872b02105c6151147f3c5b
stage=3 transaction=1 addr=00 cmd=a000 previous=0000
fault=4 result=5 cleanup=0 spi=0 tx_valid=1 rx_valid=1
TX: a0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
RX: 7f 92 00 00 00 00 00 00 00 00 00 00 00 00 00 6e 7f 00
crc_calculated=8fcd crc_received=6e7f crc_match=0
response_padding=00 crc_padding=00
adc_state=0 adc_fault=4 adc_crc=1 adc_stop_ack=1 led_attempt=0
```

Evidence class: user-reported on-target observation; no independent scope trace,
physical board identification, or bench procedure is supplied here. The STATUS
image matches SHA256 of the retained diagnostic ELF at
`ads131-startup-evidence/target-optical/noctinirs2.elf`. This is an ELF identity,
not the SHA256 of the `.bin` file. STATUS `source` is the base Git SHA and does
not by itself identify an uncommitted patch.

Independent Python `binascii.crc_hqx(data[:15], 0xffff)` confirms CRC 8FCD for
the captured bytes versus received 6E7F. For analysis only, removing the initial
zero bit and appending a zero at the end gives:

```text
ff 24 00 00 00 00 00 00 00 00 00 00 00 00 00 dc fe 00
```

CRC is DCFE and equals the received word in that analytical counterpart. FF24
matches the expected reset acknowledgment. Both padding bytes in the actual
capture are zero: this failure is numeric CRC mismatch, not padding rejection.
The first RREG frame receives a prior reset response; it is not yet the ID readback.
`spi=0` proves driver completion, not correct electrical sampling.

The production host seam now replays the exact capture and checks immediate
first-transaction CRC fault, original bytes, stage/command, both CRC values,
and reset rollback. A separate explicitly hypothetical aligned fixture passes
initialization; corrupting the following response still faults. There is no
production shift, CRC bypass, or reset-response exemption.

## Effective IDF behavior: corrections to the earlier comparison

The installed IDF v6.1 source was traced through
`esp_driver_spi/src/gpspi/spi_master.c`, `esp_hal_gpspi/spi_hal_iram.c`,
`esp_hal_gpspi/esp32s3/include/hal/spi_ll.h`, and the S3 SPI register definitions.

| Setting | Effective behavior on this driver/target |
| --- | --- |
| CS pre=0 versus pre=1 | Driver copies it to HAL `cs_setup`; HAL calls the S3 LL setter unconditionally. Zero disables `user.cs_setup`; one enables it with `cs_setup_time=0`, a one-cycle prepare phase. No hardware command/address phases are used here. |
| CS post=0 versus post=1 | Driver coerces zero `cs_hold` to one. Therefore active default post=0 and historical explicit post=1 have the same effective hold setting in IDF 6.1. |
| Duty cycle 0 versus 128 | Driver converts zero to 128. Both request 50% duty. |
| RX length 0 versus 144 | Full duplex defaults RX length to TX length. Both receive 144 bits. |
| Queued versus polling | Both enter `spi_new_trans()` and the same HAL transaction setup/start path. Their scheduling differs; they do not inherently choose different clock edges. |
| `sample_point` | S3 LL setter is unsupported/no-op; generic sample-point tuning is not an evidence-based correction on this target. |
| Generic MISO delay | S3 LL `spi_ll_set_miso_delay()` is also a no-op. Changing input-delay assumptions alone must not be presented as a measured sampling fix. |

The generic API header says pre-CS applies only to half duplex. Its wording
alone was insufficient for the earlier conclusion: the target-specific source
does program CS setup for this full-duplex, zero-command/address configuration.
The explicit legacy V3/V4 pre=1 is consequently a meaningful candidate difference.
Actual waveform timing remains unmeasured. No SDK source or registers are patched.

TI SBAS890D sections 6.6-6.8 and 8.5.1 require mode 1, launch on rising SCLK,
sample on falling SCLK, and at least 16 ns CS-low to the first rising SCLK.
At 8 MHz one SCLK period is 125 ns. Enabling one prepare cycle targets the start
boundary without inserting a dummy data clock. This static reasoning and the
coherent shifted frame support a candidate; they do not prove causation.

Sources: [TI ADS131M04 Rev. D](https://www.ti.com/lit/ds/symlink/ads131m04.pdf),
[IDF v6.1 S3 LL](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_hal_gpspi/esp32s3/include/hal/spi_ll.h),
[HAL](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_hal_gpspi/spi_hal_iram.c),
[driver](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_driver_spi/src/gpspi/spi_master.c).

## Candidate and acceptance limits

Only the transport CS setup request changes, from zero to one. A startup-only
`ads131_transport` log identifies `candidate=cs-pre1` and its requested settings.
Post-CS remains default (effectively one in this driver). Polling, SPI mode,
clock, host, pins, DMA, reset waits, first RREG, CRC, padding, ACK/readback, LED
800 kHz runtime / 100 kHz power, and Fs minimum 2 Hz are unchanged.

This isolates one timing variable. It avoids simultaneously changing to NULL,
polling, or a different reset delay, which would obscure attribution. No success
on the new candidate has been reported or observed on hardware.

Author host tests: 80 tests, zero failures/ignored, GCC 5.1.0, strict C11 with
Unity 64-bit assertions enabled. Pinned Unity hashes pass in canonical Git source
staging with exact changed-file overlays. Target build and artifact identities
are recorded in `ads131-boot38-evidence` beside the Orchestrator workspace.
The unchanged static-device budget assertion remains mandatory.

Final local result: optical compile/link PASS on IDF v6.1, Xtensa GCC 15.2.0;
no C compiler warnings. App image: 881648 bytes (144 bytes above the diagnostic
baseline); ADC static device state remains 12056 bytes, within 12288 bytes.
Historical integrity passes for all 319 files; `git diff --check` passes.
New ELF / expected STATUS `image`:
`b90685413e0be63056cddb4a5ac9b06c29b768cb9dcde85f8f95670177ff0319`.
These are author host/static/build results, not physical validation of pre-CS.

Remaining acceptance: separately authorized board testing must show the raw first
response no longer shifted, all initialization integrity/readback checks pass,
and acquisition can proceed. If the fault persists, preserve the first-failure
record and obtain CS/SCLK/MISO/MOSI timing; do not shift received data or grant
a CRC exception. A valid first response alone does not qualify sustained 8 kSPS
acquisition, LED operation, or timing coexistence.

Rollback: revert only the CS setup line/startup marker to recover the diagnostic
baseline. No merge, release, flash, or hardware operation was performed by this
agent. This document does not authorize another physical test.
