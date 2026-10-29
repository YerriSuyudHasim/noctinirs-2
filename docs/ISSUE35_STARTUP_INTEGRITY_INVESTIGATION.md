# Issue 35 startup integrity investigation

Date: 2026-10-28. Baseline: `79ebaea934dfb76c04975d9a10d6fce0117c8979`,
branch `feat/35-ads131-acquisition`. This is a local diagnostic candidate;
the reported board failure has not been reproduced on a device.

## Scope and acceptance

Preserve the first ADC startup failure before reset/STOP cleanup, identify its
transaction and integrity subtype, compare historical protocol behavior, and
exercise the diagnostic through the production host seam and target build.
Accountable activity: firmware diagnostic implementation. Required subsequent
reviews: Hardware, RTOS, and a non-author Reviewer/Verification Agent. No review
approval is supplied by this author.

The reported `adc_fault=4 adc_crc=1 led_attempt=0` is consistent with startup
exchange integrity failure before LED startup. It does not identify a command,
prove numeric CRC failure, or establish an electrical cause. The requested replay
of the actual failing response and evidence-based protocol correction remain
pending raw board TX/RX. Synthetic tests below are not board captures.

## Static comparison

Sources: active `main/adapters/nn2_ads131_logic.c`, `nn2_ads131_esp.c`,
`nn2_ads131_control.inc`, `nn2_acquisition_esp.c`, `main/nn2_board_v1.c`;
historical `reference/v3/components/nocti_v3_adc/nocti_v3_ads131m04.c`,
`reference/v4/components/nocti_v4_ads/nocti_v4_ads.c`, and V4 ADS configuration.
Historical snapshots remain read-only and are not integration evidence.

| Property | Active feat35 | V3 / V4 | Implication |
| --- | --- | --- | --- |
| Reset | LOW 251 us, release, wait 5 us | LOW 300 us, wait 20 us | Both waits meet the published 5 us register acquisition interval; board timing is unmeasured. |
| First exchange | RREG ID; its RX is checked for CRC and padding | NULL; require first response FF24 | Active does not explicitly validate the reset acknowledgment. A different command alone does not prove corruption. |
| SPI | Dedicated SPI2, mode 1, 8 MHz, queued, DMA off | Historical dedicated transport, mode 1, 8 MHz, polling, DMA off | Bus/pin assignments are not transferred. |
| CS | IDF default pre/post guards (zero) | Explicit one clock pre/post guards | Candidate difference requiring timing evidence; no guard change made. |
| Frame | Six 24-bit words, 18 bytes, MSB aligned | Same | Single-register reads do not relocate the output CRC. |
| Pipeline | RREG then NULL; WREG then NULL acknowledgment, then RREG/NULL readback | RREG then NULL; WREG followed by readback, without active's explicit ACK check | Current and previous command must both be recorded. |
| MODE | Write 0110: 24-bit, CCITT, input/register CRC off, level DRDY | RMW selects 24-bit/CCITT/input CRC off, clears reset, pulse DRDY | No active word-length or polynomial transition occurs at the MODE write. DRDY policy differs. |
| Output integrity | CRC plus response/CRC zero padding for every startup exchange | Register/reset helpers check SPI/response/readback; numeric CRC checked in sample parser | Historical startup success does not establish that every startup frame passed CRC. |
| CRC | FFFF seed, 1021 polynomial, MSB first, non-reflected, no final XOR; bytes 0..14; received at 15..16 | Same sample CRC | No evidence supports changing the CRC algorithm or coverage. |

Protocol cross-check: TI [ADS131M04 SBAS890D Rev. D](https://www.ti.com/lit/ds/symlink/ads131m04.pdf),
sections 6.7, 8.3.12, 8.4.1.2, 8.5.1.7-8, and 8.5.1.10.7-8.
The reset release wait may use tREGACQ or the DRDY rising edge. The existing
`wait_ready()` waits for DRDY LOW after configuration; it is a conversion-ready
check, not a pre-transaction SPI-ready rising-edge observation.
A correctly padded FF24 reset response with valid CRC is accepted by the active
exchange checker; the host startup model now exercises that case. An explicit
NULL/reset-ack check may improve diagnosis, but its absence is not an established
cause of the observed integrity fault.

CS qualification from the installed ESP-IDF 6.1 `spi_master.h`: the pre-transfer
CS setting only works for half-duplex transactions. All three transports use
full duplex. Thus the historical explicit pre/post values of 1/1 must not be
interpreted as proof of one extra SCLK period before the full-duplex frame.
The post-transfer setting still differs. Actual CS/SCLK boundaries need a trace.
Likewise, active `rxlength=0` is documented and implemented as `length` in full
duplex, so it still receives the complete 144 bits, like historical explicit
`rxlength=144`.

## Diagnostic contract

`nn2_ads131_logic_t.startup_failure` retains the first startup failure for the
boot lifetime. It freezes before `nn2_ads131_fail()` asserts reset. STOP, later
faults, and successful or unsuccessful reinitialization do not erase it. A fresh
boot/bind is needed for a fresh first-failure capture.

The snapshot includes stage, one-based transaction number, register address,
current/previous command, 18 TX bytes, 18 RX bytes when available, calculated and
received CRC, numeric CRC equality, both padding bytes, logical error, original
fault, and separate cleanup result. Address FF means no register transaction.
Stages are defined in `nn2_ads131_logic.h`: reset LOW/HIGH, RREG command/response,
WREG command/response, conversion readiness, and start synchronization.

SPI timeout/error means RX is unavailable. The zero-initialized RX array is not
a capture and no CRC is computed from it. The ESP adapter retains the raw SPI
driver error before cleanup can overwrite `last_esp_error`; this value is only
meaningful when TX is valid. An unexpected completion pointer remains a poisoned
transport failure, as before.

The owner emits `ads131_startup` metadata and `ads131_startup_tx` /
`ads131_startup_rx` hex dumps once after the failed owner iteration. No log is
added to successful sample processing. Snapshot copies/CRC diagnostics are
limited to startup transactions; runtime retains its existing integrity checks.
This adapter-private addition changes neither the Core observation ABI nor the
USB STATUS format. Capture the console log from startup, not STATUS alone.

## Validation and limits

Host injections cover all 14 startup exchanges, command/response stages and
addresses, CRC corruption, independently CRC-valid bad response padding, bad
CRC-word padding, SPI timeout without stale RX, cleanup reset failure, retention
through STOP/reinit, and continued runtime integrity rejection. The fake reset
response is corrected to FF24 instead of a synthetic STATUS response. It is
still a generated protocol model, not measured SPI behavior.

Validation evidence and environment details are retained in the local
`ads131-startup-evidence` folder beside the Orchestrator workspace; see its
verification record. Evidence is tied to the baseline SHA plus hashes of the
modified source files, not an unchanged-commit test claim. Historical integrity
must pass before and after the work. Target builds establish compile/link only.

No protocol correction is justified yet. LED runtime 800 kHz, power transactions
100 kHz, Fs minimum 2 Hz, pins, reset delays, SPI clock, CRC enforcement, task
priorities, and fault cleanup remain unchanged. Rollback is to discard/revert
the diagnostic-only source candidate. No merge, release, flash, board operation,
or on-target bench/HIL test was performed.

Local author validation results (not independent verification):

| Check | Result |
| --- | --- |
| Host/unit | 79 tests, 0 failures, 0 ignored; MinGW TDM GCC 5.1.0, strict C11, `UNITY_SUPPORT_64` enabled. |
| Header opacity | Positive public use compiles; ring/sequencer field access fails for incomplete types. Runner's Windows path lookup fails after the passing suite; equivalent probes passed using the retained Windows path parser. |
| Optical compile/link | PASS, ESP-IDF v6.1 / Xtensa GCC 15.2.0, ESP32-S3; external configuration enables optical acquisition and retains LED runtime 800000 Hz. |
| Image / memory | 881504-byte app image; 16% of 1 MiB app partition free. Static ADC device state is 12056 bytes, below its unchanged 12288-byte assertion. No comparable pre-change optical size delta is claimed. |
| Warnings | No C compiler warnings/errors in the optical build; SDK Kconfig notices and CMake long-path warnings are retained in the log. |
| Historical integrity | 319 files verified before and after; no historical edits. |
| Working-tree whitespace | `git diff --check` passed. |
| Actual failing transaction / hardware | NOT RUN / unidentified; reported board RX is unavailable. |

The host source staging uses canonical Git archive bytes to satisfy pinned Unity
hashes (the Windows checkout's license has CRLF), then overlays the exact four
changed source/test files. Earlier failed environment attempts are retained.
No dependency bytes or assertions were weakened to obtain the passing result.

Next action after specialist review and separately authorized board procedure:
capture the first-failure console record for the same START attempt, preserve
firmware identity and STATUS, replay those exact bytes in a host regression, and
only then select the smallest supported protocol/timing correction. Do not
classify a padding-only failure as numeric CRC failure or hardware noise.
