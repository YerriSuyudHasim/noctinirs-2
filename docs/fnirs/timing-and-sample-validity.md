# Define fNIRS timing windows, sample validity and K selection

Status: **PROPOSED / AUTHOR WORK REQUIRED**. Initial research and scope draft; not an accepted final contract.
Tracking issue: [#67](https://github.com/YerriSuyudHasim/noctinirs-2/issues/67).

## Purpose and status

Define when a sample genuinely represents a stable LED ON/OFF interval, accounting for analog settling, ADC filter memory and timestamp meaning.

This is one of five documentation workstreams published at the Owner's direct request. It preserves the existing board implementation. Unresolved technical choices are intentionally visible rather than filled with assumed values. Completing this initial draft does not complete the issue's acceptance criteria.

## Initial source findings

V3's timing document budgets 8000 SPS (125 us nominal period), 20 optical slots per 100 ms frame, and 2500 us ON/OFF half-slots at 10 optical frames/s. It uses a 250 us LED commit design allowance, 500 us settling allowance and an additional 400 us OFF guard. The corresponding nominal windows are 1750 us ON and 1350 us OFF, or 14 and 10.8 sample periods. These are budgets, not guaranteed accepted counts.

Historical target K is 10 ON and 10 OFF, with minimum8/8. V4 config declares three post-SYNC discarded frames. Neither policy may be adopted without checking actual filter response, event alignment and the intended optical rate. A DRDY timestamp marks output availability; SPI completion marks reception, and neither alone proves the input illumination interval was stable.

For any accepted sample, record enough identity to associate the acquisition epoch, detector, LED/source/wavelength, window, timestamp meaning and rejection reason. Define exact boundary behavior before coding the classifier. Account for analog settling and filter memory rather than applying an unexplained universal group-delay subtraction.

The board candidate advertises integer optical Fs2..10. Produce an explicit feasibility row per supported rate/profile, including actual valid sample counts, target/minimum K and partial/invalid behavior. This draft does not attest that feat/33 already implements this contract or synchronized optical capture.

## Source baseline

Main baseline: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1`. Board comparison baseline: `197134ec0fd72363e4e814927812c4d48417ddf0` on `feat/33-board-bringup`. Historical evidence is static and remains subject to reconciliation with the actual assembled board.

- [reference/v3/docs/V3_FINAL_TIMING_CALCULATION.md](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v3/docs/V3_FINAL_TIMING_CALCULATION.md)
- [reference/v4/components/nocti_v4_common/include/nocti_v4_ads_config.h](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v4/components/nocti_v4_common/include/nocti_v4_ads_config.h)
- [docs/LED_ASYNC_RUNTIME.md](https://github.com/YerriSuyudHasim/noctinirs-2/blob/197134ec0fd72363e4e814927812c4d48417ddf0/docs/LED_ASYNC_RUNTIME.md)
- [docs/LED_HOST_CONTROL.md](https://github.com/YerriSuyudHasim/noctinirs-2/blob/197134ec0fd72363e4e814927812c4d48417ddf0/docs/LED_HOST_CONTROL.md)

## Dependencies

Final window/K decisions depend on the analog/ADC profile and actual LED timing behavior.

Related workstreams: [electrodes #64](https://github.com/YerriSuyudHasim/noctinirs-2/issues/64), [ADS1299 #65](https://github.com/YerriSuyudHasim/noctinirs-2/issues/65), [analog/ADC #66](https://github.com/YerriSuyudHasim/noctinirs-2/issues/66), [timing/K #67](https://github.com/YerriSuyudHasim/noctinirs-2/issues/67), [performance #68](https://github.com/YerriSuyudHasim/noctinirs-2/issues/68).

## Remaining author work and completion criteria

## Problem and outcome

A sample read during an LED slot may still represent a transition or prior illumination because of analog and ADC filter response. Define when each sample is valid for an ON/OFF measurement and how enough valid samples are selected at each supported optical rate.

## Deliverable

Proposed file: docs/fnirs/timing-and-sample-validity.md.

- Draw the full timeline: LED request/transfer/completion, physical transition uncertainty, analog settling, ADC/filter response, DRDY, read and classification.
- Define a common time domain and distinguish event time, sample-output time and estimated input-signal interval. Do not universally correct group delay by an unexplained timestamp subtraction.
- Define initial/restart/SYNC behavior, filter settling and discard rules, configuration epochs and stale-sample rejection.
- Specify half-open ON/OFF windows, guard intervals, actual timing inputs and handling of uncertain/missed transitions.
- Analyze every proposed optical frame rate, including currently advertised integer Fs2..10 if retained, against acquisition rate and available windows. Accept, constrain or mark unsupported each combination explicitly.
- Derive target/minimum K from signal-quality requirements and valid sample availability. Evaluate historical K10/10 and minimum8/8; do not adopt them without rationale.
- State whether K is fixed or rate/profile dependent, which samples are selected, whether selection uses contiguous newest samples, and how invalid/gap samples affect the result.
- Define ON/OFF pairing, corrected intensity, counts, timestamps, partial/invalid flags and reason codes. Do not silently borrow samples from another slot or epoch.
- Calculate nominal budgets and margins; separate them from measured worst-case timing.
- Provide worked boundary examples and a future validation plan using synthetic timestamp cases and approved waveform/sample evidence.

## Acceptance criteria

- Every accepted sample belongs unambiguously to one eligible channel/slot/window/epoch.
- Filter memory and analog settling are addressed, not only SPI-read timestamp.
- Each supported rate/profile has a documented K feasibility calculation and insufficient-data policy.
- Edge equality, DRDY phase offsets, jitter, missed events, STOP/restart and sync transitions have explicit expected classifications.
- Nominal 24-bit format or CRC success is never presented as proof of correct optical timing.

## Exclusions and dependencies

No scheduler rewrite, hardware execution or claim that the current firmware meets the contract. Timeline work can begin now; final valid-window/K decisions depend on the analog/ADC workstream (#66) and current LED timing behavior. Keep fNIRS timing out of ADS1299 driver scope.

## Verification and limitations

Publication validation is limited to changed-path review, whitespace and historical reference integrity. No firmware/runtime configuration is changed. Compiler/unit execution, target builds and hardware/human measurements are NOT RUN for this draft. Calculations and historical comments are not measured results. Specialist assessment and eligible independent review remain pending before final acceptance.

Rollback of this proposed document is a normal Git revert; it does not change an image already on a board. No firmware, sdkconfig, workflow, protection setting, release, flash or physical operation is authorized by this document.
