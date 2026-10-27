# Define the eight-channel EEG/EOG/EMG/ECG measurement and electrode map

Status: **PROPOSED / AUTHOR WORK REQUIRED**. Initial research and scope draft; not an accepted final contract.
Tracking issue: [#64](https://github.com/YerriSuyudHasim/noctinirs-2/issues/64).

## Purpose and status

Recover the historical 4 EEG + chin EMG + vertical/horizontal EOG + ECG profile and turn it into an unambiguous connector, input-polarity, reference and bias contract.

This is one of five documentation workstreams published at the Owner's direct request. It preserves the existing board implementation. Unresolved technical choices are intentionally visible rather than filled with assumed values. Completing this initial draft does not complete the issue's acceptance criteria.

## Initial source findings

The recovered V2 note records the following intended mixed profile. This is historical design intent, not a captured current register image or a verified electrode wiring map.

| Channel | Signal | Historical gain | Historical topology |
|---|---|---|---|
| CH1-4 | EEG | 24 | Referential, SRB2 enabled; reference described as mastoid |
| CH5 | Chin EMG | 8 | Bipolar |
| CH6 | Vertical EOG | 4 | Bipolar |
| CH7 | Horizontal EOG | 4 | Bipolar |
| CH8 | ECG Lead I | 4 | Bipolar |

The note specifies 1000 SPS and bias masks 0xFF/0xF0. It also describes BIASOUT at mastoid A1/A2. Exact EEG sites, distinct reference/bias electrode identities, connector polarity and all site-to-input connections remain unresolved. Do not interpret the two mastoid references as an instruction to join reference and driven bias.

The library defaults are different: 250 SPS, gain12, all channels disabled/shorted and SRB2 false. The inspected V2 fNIRS application explicitly leaves ADS1299 unused. V3's migration map defers the ADS1299 mini-PSG; the V4 snapshot's ADS driver is ADS131M04. None establishes deployment of the intended mixed montage.

An additional local archival ECG example examined during research enabled CH8 only at 250 SPS/gain12 and called it Lead II. That local example is not committed evidence in this PR. Its exact source must be published with provenance, or the lead/polarity conflict independently resolved, before it becomes part of the final contract.

## Source baseline

Main baseline: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1`. Board comparison baseline: `197134ec0fd72363e4e814927812c4d48417ddf0` on `feat/33-board-bringup`. Historical evidence is static and remains subject to reconciliation with the actual assembled board.

- [reference/v2/CLAUDE.md](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v2/CLAUDE.md)
- [reference/v2/components/nocti_biopot/include/nocti_biopot.h](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v2/components/nocti_biopot/include/nocti_biopot.h)
- [reference/v3/docs/V3_MIGRATION_MAP.md](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v3/docs/V3_MIGRATION_MAP.md)

## Dependencies

Independent starting point; feeds ADS1299 register and acquisition decisions.

Related workstreams: [electrodes #64](https://github.com/YerriSuyudHasim/noctinirs-2/issues/64), [ADS1299 #65](https://github.com/YerriSuyudHasim/noctinirs-2/issues/65), [analog/ADC #66](https://github.com/YerriSuyudHasim/noctinirs-2/issues/66), [timing/K #67](https://github.com/YerriSuyudHasim/noctinirs-2/issues/67), [performance #68](https://github.com/YerriSuyudHasim/noctinirs-2/issues/68).

## Remaining author work and completion criteria

## Problem and outcome

The historical mixed-biopotential profile assigns four EEG channels, chin EMG, vertical/horizontal EOG and ECG, but does not fully define connector routing, electrode polarity, reference/bias separation or measurement locations. A separate old ECG example contradicts the mixed-profile lead designation. Produce one explicit measurement contract for the existing board.

## Deliverable

Proposed file: docs/biopotential/measurement-and-electrodes.md.

- Identify the actual ADS1299 variant and available populated/routed inputs.
- Map CH1-8 to signal, physical connector/pin, INxP/INxN, SRB routing, electrode label and intended measurement polarity.
- Use the historical 4EEG + 1EMG + 2EOG + 1ECG allocation as the starting proposal. Distinguish documented intent from inspected implementation.
- Specify EEG sites and reference/bias roles through an explicit Owner decision; retain unknowns visibly until decided.
- Explain electrode reference, driven bias and ADC voltage reference as separate functions.
- Resolve ECG Lead I versus the old Lead II example and its polarity wording.
- Define desired bandwidth and amplitude/offset envelope for each modality as requirements feeding the ADS1299 workstream (#65); do not claim clinical adequacy from historical gains.
- Record input protection, supply/common-mode and isolation facts needed to assess the connection scheme. This document is not a human-test authorization.

## Acceptance criteria

- Every channel has a complete electrical-to-signal mapping or a named unresolved decision that prevents approval.
- No ambiguity about connector polarity, shared versus differential references, or distinct reference/bias electrodes.
- Historical mixed and ECG-only configurations are separated and source-linked.
- No invented electrode site or measured board property.

## Exclusions and dependencies

No driver, register implementation, DSP or physical acquisition. Can start immediately from current schematic/library sources. Its mapping and signal requirements feed the ADS1299 workstream (#65).

## Verification and limitations

Publication validation is limited to changed-path review, whitespace and historical reference integrity. No firmware/runtime configuration is changed. Compiler/unit execution, target builds and hardware/human measurements are NOT RUN for this draft. Calculations and historical comments are not measured results. Specialist assessment and eligible independent review remain pending before final acceptance.

Rollback of this proposed document is a normal Git revert; it does not change an image already on a board. No firmware, sdkconfig, workflow, protection setting, release, flash or physical operation is authorized by this document.
