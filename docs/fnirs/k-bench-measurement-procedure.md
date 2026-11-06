# K selection: bench measurement procedure (DRAFT, not authorized)

Status: **DRAFT / NOT AUTHORIZED / NOT RUN.** This document proposes a procedure. It does not
grant permission to energize emitters or flash a device. Class H work needs a specific
written Owner authorization (AGENTS.md; ADR-0002 section 3.6, Q-5).

Companion to [k-selection-by-sampling-rate.md](k-selection-by-sampling-rate.md). Draft authored
by Claude Code after reviewing that document. The author of this draft cannot be its
independent reviewer. Firmware, `sdkconfig`, and ADR files are not changed by this draft.

## 1. Owner direction recorded from chat (not yet an ADR amendment)

| Item | Direction given in chat | Formal status |
|---|---|---|
| Phase split, science profile | 50/50 (ADR-0002 R13) | Firmware default is still 44 %. Config change or amendment pending. |
| 1 Hz | Keep as a required rate | Firmware still rejects it (`nocti_v4_sample_fs_allowed`). Change pending. |
| K, K_min | Keep 10 / 8 until data exists | ADR-0002 R11 unchanged. |
| This procedure | Owner agreed to a draft | Execution still needs section 8 authorization. |

## 2. Goal

Replace three unmeasured assumptions in the K draft with measured values:

1. How much the mean of K samples reduces noise (sinc3 neighbours are correlated).
2. How many samples after an LED edge are really unsettled (placeholder is 4).
3. Which error dominates: ADC noise, LED drift, ambient, or settling.

Output: a table of K and K_min per Fs with the data behind each number. Evidence class:
**on-target bench**. It is not clinical and not a safety result.

## 3. Prerequisites (each must be checked before any run)

| # | Prerequisite | Status |
|---|---|---|
| P1 | RAW tap in ENGINEERING mode delivers every sample with its phase class, including settle-class samples | **Not verified.** Check `stage_raw` in `nocti_v4_optical.c` and the RAW codec. If settle samples are dropped, T2 needs a firmware change (separate scope). |
| P2 | 50 % phase selectable while STOPPED | `NOCTI_V4_OFF_PHASE_QUALIFICATION_US` exists and `NOCTI_V4_OFF_PHASE_IS_SUPPORTED` accepts it. Confirm how it is selected on the control path. |
| P3 | 1 Hz runnable | **Blocked.** Needs a separate firmware change to `nocti_v4_sample_fs_allowed` and RAW buffer/UDP checks. Run 2-10 Hz first. |
| P4 | RAW capacity | Buffer holds only about 12-16 ms at 8 kSPS (96 or 128 entries). UDP loss is expected. Windows touching a gap are excluded (section 6). |
| P5 | Reference baseline | Normal-mode 10 Hz run still meets the recorded limits (`fault=0`, `edge_miss=0`, `spi_max <= ~60`, `service_max <= ~100`). |
| P6 | Procedure reviewed by a hardware specialist | Pending. |

## 4. Setup (assumptions to confirm with the Owner)

- No human subject. Fixed, static target (for example a diffuse phantom or reflector) in a
  closed dark enclosure. Static ambient. Same board and configuration for all runs.
- LED current within the ADR-0002 bring-up band (15-30 mA). Use one fixed value per series.
  Higher currents are not part of this procedure.
- Record: ESP-IDF version, toolchain, target, board revision (unknown today), `sdkconfig`
  hash, firmware commit SHA, binary size, enclosure temperature, date.
- Capture with `tools/nn2_raw_capture.py` (binary + CSV). Keep the original datagrams.
- Data contains no personal data. Do not commit raw captures. Store them outside the repo and
  commit only the summary table and checksums.

## 5. Tests

Phase for all tests: **50/50**. Repeat T1 once at 44 % at 10 Hz as a comparison for the
open question on unequal windows.

### T1: noise versus K (answers goal 1 and 3)

- Rates: 2, 4, 5, 8, 10 Hz (and 1 Hz after P3).
- Per rate, capture at least **1000 frames**. Arithmetic (HITUNGAN): the relative error of a
  standard deviation from N independent windows is about `1/sqrt(2(N-1))`, so N=1000 gives
  about 2 %. Duration is 1000 / Fs seconds (100 s at 10 Hz, 1000 s at 1 Hz).
- Add one 600 s run at 10 Hz to expose slow drift.
- Use the OFF window as the pure ADC and ambient noise floor. Use the ON window for noise plus
  LED drift. The difference between the two is the discriminator for "dominant error".
- Offline analysis, per slot and per channel:
  1. From stable-class samples only, form non-overlapping groups of K samples inside each
     window for K = 1, 2, 3, ..., up to the stable window length.
  2. Compute the standard deviation of the group means across frames, giving `sigma(K)`.
  3. Compare with the white-noise prediction `sigma(1)/sqrt(K)`. The ratio is the effective
     independence of neighbouring samples.
  4. Compute the autocorrelation of consecutive samples inside a window.
  5. Also compute the Allan deviation of per-frame window means to find the drift floor.
- Report: `sigma(K)` curve with confidence interval, the K where it stops falling, and the
  ratio to the white-noise prediction.

### T2: settling after an edge (answers goal 2)

- Use a stable signal level with a large ON/OFF step. Collect, per edge, the samples from
  the edge index onward, aligned by `first_sample_sequence`.
- Average the aligned traces over at least 1000 edges, ON-edge and OFF-edge separately.
- Report the first sample index after which the mean stays within a stated fraction of the
  final value, with the noise-limited resolution. The tolerance (for example 1 LSB-equivalent
  or 0.1 % of the step) is an Owner decision and must be fixed before looking at data.
- Compare with the placeholder `NOCTI_V4_SAMPLE_SETTLE = 4` and with 3 sinc3 periods.
- Depends on P1.

### T3: commit lateness margin (answers K_min margin)

- At 10 Hz, 50 %, run 600 s with `nn2ctl timing` before and after. Record `on_d`, `off_d`,
  `sample_clock_late_commits`, `async_latency_max_us`.
- Compute the worst observed late shift in samples. The remaining stable window is
  `stable - late_shift`. K_min must not exceed the worst case with a stated margin.
- Existing host tests cannot establish this. It is on-target only.

### T4: regression

- After each series, repeat the normal-mode 10 Hz 600 s baseline check from P5.
- Any change in `fault`, `edge_miss`, `adc_missed`, `gap`, `crc_bad`, or `spi_errors`
  from the baseline is a stop condition.

## 6. Data rejection rules (fixed before looking at data)

- Exclude any window that contains a RAW gap, a `GAP_BEFORE` flag, a sample-index break, or a
  CRC failure.
- Report the number of windows excluded per run. If more than 10 % are excluded, repeat
  the run. Do not interpolate.
- Do not remove outliers by hand. If an outlier rule is needed, state it first and report
  results with and without it.

## 7. K selection rule (proposal, to be fixed by the Owner before data)

For each Fs, `K_target` is the smallest K such that `sigma(K)` is within a tolerance `t` of the
lowest `sigma` seen on the curve (default proposal `t = 10 %`), capped by
`stable_window - worst_late_shift - margin`. `K_min` is a fraction of `K_target` that still
keeps the noise within a second, looser tolerance. Both tolerances are Owner decisions.
The result is a table per Fs plus one ADR-0002 amendment that cites this data.
If the curve is flat above K = 10 for every Fs, the correct outcome is to keep 10 / 8 and
record the evidence.

## 8. Authorization and stop conditions

Execution requires, in writing, from the Owner: the board and revision, LED current, enclosure
and target description, the exact commit SHA to flash, and permission to flash and energize
emitters. Without all of these, none of this procedure runs.

Stop and report if: a fault counter changes from baseline, a temperature or current limit is
approached, any part of the setup differs from the approved description, a secret is found, or
the RAW capture shows systematic loss that cannot be separated from the signal.

## 9. Not covered

Clinical validity, human measurements, safety, RF, thermal limits, current above the
bring-up band, firmware changes (including 1 Hz and K policy), and any merge or release.
Hardware timing is NOT RUN until this procedure is executed.
