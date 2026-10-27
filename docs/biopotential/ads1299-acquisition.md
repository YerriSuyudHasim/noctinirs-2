# Define ADS1299 registers, SPI acquisition and sample integrity

Status: **PROPOSED / AUTHOR WORK REQUIRED**. Initial research and scope draft; not an accepted final contract.
Tracking issue: [#65](https://github.com/YerriSuyudHasim/noctinirs-2/issues/65).

## Purpose and status

Specify how the existing board produces eight correctly scaled, time-identified and loss-accounted biopotential channels, correcting historical library register ambiguities.

This is one of five documentation workstreams published at the Owner's direct request. It preserves the existing board implementation. Unresolved technical choices are intentionally visible rather than filled with assumed values. Completing this initial draft does not complete the issue's acceptance criteria.

## Initial source findings

The V2 library supplies a useful starting point: SDATAC/START/RDATAC startup, one 27-byte read per sample (3 status bytes plus eight signed 24-bit channels), contiguous polling for transfers up to 32 bytes, and a configurable optional queue. Its task records a timestamp after DRDY waiting rather than capturing the edge timestamp in the ISR; optional queue insertion failures are ignored.

At 1000 frames/s, raw sample payload is 27000 bytes/s before transport metadata. A 27-byte frame requires 216 SCLK cycles: 27 us at an illustrative 8 MHz or 54 us at 4 MHz, excluding command/CS/software overhead. These arithmetic budgets do not select or qualify the final bus clock.

The legacy driver has concrete review items: LOFF bit4 is written as an AC excitation enable although TI marks it reserved/zero; CONFIG1.DAISY_EN=0 is described as disabling daisy mode although TI defines zero as daisy-chain mode; rate code seven is accepted despite being reserved. CONFIG2 and MISC1 are not explicitly programmed by the inspected full-setup routine. SRB2 path comments and bias-mask selection must be checked against the input MUX and actual board wiring.

Historical bus assignments differ: V2 prose says ADS1299 SPI3, while the inspected fNIRS-only composition root initializes its unused ADS1299 device on SPI2/4 MHz; V3 board defaults specify ADS1299 SPI2/8 MHz. Select the final host using current composition and board wiring, not a legacy alias.

The final register table, sample-rate/gain decision, readback masks and complete validation procedures remain author work.

## Source baseline

Main baseline: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1`. Board comparison baseline: `197134ec0fd72363e4e814927812c4d48417ddf0` on `feat/33-board-bringup`. Historical evidence is static and remains subject to reconciliation with the actual assembled board.

- [reference/v2/components/nocti_biopot/nocti_biopot.c](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v2/components/nocti_biopot/nocti_biopot.c)
- [reference/v2/components/nocti_biopot/include/nocti_biopot.h](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v2/components/nocti_biopot/include/nocti_biopot.h)
- [reference/v3/components/nocti_v3_board/nocti_v3_board.c](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v3/components/nocti_v3_board/nocti_v3_board.c)
- [TI ADS1299 datasheet Rev C](https://www.ti.com/lit/ds/symlink/ads1299.pdf)

## Dependencies

Final gain, reference, SRB and bias choices depend on the measurement/electrode contract.

Related workstreams: [electrodes #64](https://github.com/YerriSuyudHasim/noctinirs-2/issues/64), [ADS1299 #65](https://github.com/YerriSuyudHasim/noctinirs-2/issues/65), [analog/ADC #66](https://github.com/YerriSuyudHasim/noctinirs-2/issues/66), [timing/K #67](https://github.com/YerriSuyudHasim/noctinirs-2/issues/67), [performance #68](https://github.com/YerriSuyudHasim/noctinirs-2/issues/68).

## Remaining author work and completion criteria

## Problem and outcome

The existing library provides an acquisition foundation but its defaults differ from the intended montage and several register interpretations need correction. Define exactly how each selected operating mode produces correctly identified, loss-accounted samples on the existing board.

## Deliverable

Proposed file: docs/biopotential/ads1299-acquisition.md.

- Confirm GPIO wiring, controller selection and allocation alongside ADS131M04; use SPI2_HOST/SPI3_HOST and explain any legacy FSPI/HSPI alias rather than assuming equivalence.
- Specify clock source, SPI mode/SCLK, CS timing, command decode delays, power/reset startup and shutdown order against the datasheet.
- Provide the complete intended register image per mode: address, value/mask, meaning, reserved bits and readback rule. Include normal mixed recording, input-short/internal-test diagnostics and a separately bounded lead-off/impedance mode.
- Resolve LOFF reserved-bit4 misuse, DAISY_EN interpretation, reserved rate code acceptance, SRB routing and reliance on unprogrammed defaults.
- Derive common sample rate, per-channel gain and scale conversion from the electrode-map workstream (#64)'s signal/bandwidth/headroom requirements. State clipping/noise tradeoffs.
- Define RDATAC/DRDY/full-frame transaction behavior, status checks, sign extension, event timestamps, sequence/gap detection and overload behavior.
- Compare contiguous polling/no-DMA to queued/DMA using a timing/CPU budget and future benchmark plan. Do not claim one wins without evidence.
- Specify ownership, bounded buffering and network handoff without letting Wi-Fi block acquisition.

## Acceptance criteria

- Register table is consistent with the chosen montage and primary datasheet; reserved bits and validation masks are explicit.
- Every sample has defined channel order, polarity, scale, time meaning and loss indicators.
- Selected rates have a complete transfer/latency/buffer budget, including command and software overhead allowances.
- Design includes reproducible future checks for known input, shorted noise, startup readback, missing DRDY, overload and concurrent traffic.

## Exclusions and dependencies

No driver port or device test. Routing/source audit can start alongside the electrode-map workstream (#64); final gain, SRB and bias settings depend on the electrode-map workstream (#64).

## Verification and limitations

Publication validation is limited to changed-path review, whitespace and historical reference integrity. No firmware/runtime configuration is changed. Compiler/unit execution, target builds and hardware/human measurements are NOT RUN for this draft. Calculations and historical comments are not measured results. Specialist assessment and eligible independent review remain pending before final acceptance.

Rollback of this proposed document is a normal Git revert; it does not change an image already on a board. No firmware, sdkconfig, workflow, protection setting, release, flash or physical operation is authorized by this document.
