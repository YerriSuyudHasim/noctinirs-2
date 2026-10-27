# Characterize the detector–TIA–ADS131M04 analog chain and conversion profile

Status: **PROPOSED / AUTHOR WORK REQUIRED**. Initial research and scope draft; not an accepted final contract.
Tracking issue: [#66](https://github.com/YerriSuyudHasim/noctinirs-2/issues/66).

## Purpose and status

Establish the actual four-detector routing, analog gain/filter response, dynamic range and ADC operating profile instead of treating nominal 24-bit output as measured resolution.

This is one of five documentation workstreams published at the Owner's direct request. It preserves the existing board implementation. Unresolved technical choices are intentionally visible rather than filled with assumed values. Completing this initial draft does not complete the issue's acceptance criteria.

## Initial source findings

The V4 source declares ADS131M04 with four channels, 24-bit words, OSR512 and an 8.192 MHz external clock, producing nominal 8000 SPS/channel. The gain register is set to zero, documented as gain1. These are source settings, not measured effective resolution or board headroom.

The detector documentation describes OPA2380 transimpedance paths with nominal 100 kohm feedback and 0.5 pF feedback capacitance, followed by further RC filtering. Each value, channel assignment and supply connection must be re-read from the authoritative schematic and BOM before the complete analog analysis is accepted.

For the PD4 input filter, the Owner PNG was visually checked during research: OUTPD4 goes through optode R4 (1 kohm) to AIN1P; GND goes through optode R5 (1 kohm) to AIN1N; C1 (10 nF) connects the ADC-side nodes. The earlier suggestion to replace R4 with R5 on the positive path was withdrawn. Do not propagate that reversed correction.

Rf*Cf alone does not establish complete settling: op-amp response, photodiode/input capacitance, downstream filters, ADC digital response and saturation recovery need consideration. No analog step-response measurement or measured ENOB is supplied by this draft.

## Source baseline

Main baseline: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1`. Board comparison baseline: `197134ec0fd72363e4e814927812c4d48417ddf0` on `feat/33-board-bringup`. Historical evidence is static and remains subject to reconciliation with the actual assembled board.

- [Schematic_and_hardwareInfo/SCH_PD_board_2026-10-16.json](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/Schematic_and_hardwareInfo/SCH_PD_board_2026-10-16.json)
- [Schematic_and_hardwareInfo/Schematic_PD-board_2026-10-16.png](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/Schematic_and_hardwareInfo/Schematic_PD-board_2026-10-16.png)
- [reference/v4/components/nocti_v4_common/include/nocti_v4_ads_config.h](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v4/components/nocti_v4_common/include/nocti_v4_ads_config.h)
- [TI ADS131M04 datasheet](https://www.ti.com/lit/ds/symlink/ads131m04.pdf)

## Dependencies

Independent starting point; feeds the fNIRS timing/window/K contract.

Related workstreams: [electrodes #64](https://github.com/YerriSuyudHasim/noctinirs-2/issues/64), [ADS1299 #65](https://github.com/YerriSuyudHasim/noctinirs-2/issues/65), [analog/ADC #66](https://github.com/YerriSuyudHasim/noctinirs-2/issues/66), [timing/K #67](https://github.com/YerriSuyudHasim/noctinirs-2/issues/67), [performance #68](https://github.com/YerriSuyudHasim/noctinirs-2/issues/68).

## Remaining author work and completion criteria

## Problem and outcome

A nominal 24-bit ADC and a selected sample rate do not establish detector dynamic range, useful resolution or settling. Document what voltages/currents the existing four detector paths can represent and derive a defensible ADC operating profile.

## Deliverable

Proposed file: docs/fnirs/analog-chain-and-adc.md.

- Verify PD-to-TIA-to-filter-to-ADC mapping from schematic/source, explicitly retaining the corrected R4/R5 interpretation where applicable.
- Inventory feedback R/C and downstream filters for all four paths; compare channel matching and likely tolerance effects. Distinguish calculated response from measured response.
- Analyze transimpedance gain, polarity, supply/output headroom, input common-mode and saturation/recovery constraints using actual component specifications.
- Calculate relevant filter responses and settling bounds with assumptions; identify missing photodiode/op-amp/board parameters rather than deriving settling from Rf*Cf alone.
- Define ADS131M04 CLKIN, word length, OSR/rate, PGA gain, full scale, channel enable, CRC/status and synchronization settings.
- Explain raw-code-to-voltage/current conversion and the difference between nominal word length, noise-free resolution and measured effective resolution.
- Compare the V3/V4 profile with intended current operation. Consider gain/rate alternatives based on dynamic range and noise, without changing firmware.

## Acceptance criteria

- Four detector paths and component sources are traceable; uncertainty and channel mismatch are visible.
- The selected/candidate ADC profile has explicit register meaning, scaling, headroom and noise/settling rationale.
- No calculated analog or digital response is labeled measured.
- Outputs provide the settling/filter information and constraints needed by the timing/K workstream (#67), including unresolved measurements.

## Exclusions and dependencies

No PCB modification, firmware changes, optical qualification or live measurements. Can proceed alongside PRs 1 and 2.

## Verification and limitations

Publication validation is limited to changed-path review, whitespace and historical reference integrity. No firmware/runtime configuration is changed. Compiler/unit execution, target builds and hardware/human measurements are NOT RUN for this draft. Calculations and historical comments are not measured results. Specialist assessment and eligible independent review remain pending before final acceptance.

Rollback of this proposed document is a normal Git revert; it does not change an image already on a board. No firmware, sdkconfig, workflow, protection setting, release, flash or physical operation is authorized by this document.
