# Define the ESP32-S3 combined-acquisition performance profile

Status: **PROPOSED / AUTHOR WORK REQUIRED**. Initial research and scope draft; not an accepted final contract.
Tracking issue: [#68](https://github.com/YerriSuyudHasim/noctinirs-2/issues/68).

## Purpose and status

Turn CPU, memory, interrupt and Wi-Fi tuning into measurable system choices for both ADCs plus LED/control traffic on ESP-IDF 6.1.

This is one of five documentation workstreams published at the Owner's direct request. It preserves the existing board implementation. Unresolved technical choices are intentionally visible rather than filled with assumed values. Completing this initial draft does not complete the issue's acceptance criteria.

## Initial source findings

Both historical defaults select 240 MHz CPU and a 1 kHz RTOS tick. V3 selects performance compiler optimization and main/network affinity on CPU0 with real-time work on CPU1. V4 moves main initialization to CPU1 for interrupt ownership and enables additional IRAM settings; its generated config does not enable performance compiler optimization. Defaults and generated config are distinct evidence.

V4 reduces Wi-Fi static RX/dynamic RX/dynamic TX buffers to four each for its documented approximately 20 kB/s fNIRS network workload. Adding the historical 1000 SPS ADS1299 profile contributes another 27 kB/s raw sample payload before metadata and packet overhead. Those buffer settings are a memory tradeoff, not proof of a maximum-performance combined profile.

Keep this PR as a design/benchmark document. Define sustainable rates and latency/loss targets first, then compare CPU/core/interrupt, clock/power, memory/cache/IRAM, Wi-Fi/lwIP and logging settings against them. Do not copy IDF5.5 options into IDF6.1 without checking symbol meaning and actual effective config.

The current board's PSRAM/shared-pin restriction and device/bus electrical limits remain constraints. This draft provides no runtime benchmark, no thermal result and no new sdkconfig values selected for activation.

## Source baseline

Main baseline: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1`. Board comparison baseline: `197134ec0fd72363e4e814927812c4d48417ddf0` on `feat/33-board-bringup`. Historical evidence is static and remains subject to reconciliation with the actual assembled board.

- [reference/v3/sdkconfig.defaults](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v3/sdkconfig.defaults)
- [reference/v3/sdkconfig](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v3/sdkconfig)
- [reference/v4/sdkconfig.defaults](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v4/sdkconfig.defaults)
- [reference/v4/sdkconfig](https://github.com/YerriSuyudHasim/noctinirs-2/blob/5958a0eafec6b03c2b158c4026f3f10b8a3366d1/reference/v4/sdkconfig)
- [docs/LED_ASYNC_RUNTIME.md](https://github.com/YerriSuyudHasim/noctinirs-2/blob/197134ec0fd72363e4e814927812c4d48417ddf0/docs/LED_ASYNC_RUNTIME.md)

## Dependencies

Inventory can start now; final budgets depend on the selected biopotential, analog and timing profiles.

Related workstreams: [electrodes #64](https://github.com/YerriSuyudHasim/noctinirs-2/issues/64), [ADS1299 #65](https://github.com/YerriSuyudHasim/noctinirs-2/issues/65), [analog/ADC #66](https://github.com/YerriSuyudHasim/noctinirs-2/issues/66), [timing/K #67](https://github.com/YerriSuyudHasim/noctinirs-2/issues/67), [performance #68](https://github.com/YerriSuyudHasim/noctinirs-2/issues/68).

## Remaining author work and completion criteria

## Problem and outcome

Historical sdkconfig settings optimize different fNIRS-only workloads. Adding biopotential capture changes CPU, interrupt, RAM and Wi-Fi requirements. Define measurable system targets and candidate IDF6.1 settings for sustained operation within the actual board's limits.

## Deliverable

Proposed file: docs/platform/esp32s3-performance-profile.md.

- Inventory V3/V4 defaults and generated configs versus current IDF6.1 settings; flag removed/renamed symbols and defaults that differ from generated values.
- Define target optical/biopotential rates, sample loss tolerance, latency/jitter, stream throughput, memory margin, power and temperature reporting.
- Budget both ADC buses, LED I2C, processing, packetization and control traffic. Include metadata, burstiness and network loss behavior.
- Explain CPU frequency, power-management policy, core/interrupt allocation, RTOS tick, compiler optimization, flash/cache/IRAM choices and watchdog coverage.
- Explain Wi-Fi/lwIP buffer, aggregation, scheduling and logging choices against measured or calculated workload; do not merely maximize every buffer or clock.
- Preserve board-specific memory/pin restrictions, including the PSRAM conflict, and the actual external-device clock limits.
- Propose a small benchmark matrix from baseline to individual changes and combined load, with counters, duration, measurement method and rollback criteria.

## Acceptance criteria

- Every proposed setting has a reason, resource cost and verification method.
- Workload and RAM budgets include both ADC streams and current control/LED tasks.
- Throughput success cannot hide acquisition gaps, deadlines, watchdog starvation or excessive memory use.
- Candidate settings remain clearly separated from measured optimum and qualified hardware limits.

## Exclusions and dependencies

Documentation and benchmark design only; no sdkconfig edits, performance claims or unapproved device operation. Inventory can start immediately, but final budgets use decisions from PRs 1-4.

## Verification and limitations

Publication validation is limited to changed-path review, whitespace and historical reference integrity. No firmware/runtime configuration is changed. Compiler/unit execution, target builds and hardware/human measurements are NOT RUN for this draft. Calculations and historical comments are not measured results. Specialist assessment and eligible independent review remain pending before final acceptance.

Rollback of this proposed document is a normal Git revert; it does not change an image already on a board. No firmware, sdkconfig, workflow, protection setting, release, flash or physical operation is authorized by this document.
