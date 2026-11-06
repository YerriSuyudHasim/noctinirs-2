# K and K-minimum selection per optical sampling rate (discussion draft)

Status: **PROPOSED / DISCUSSION ONLY.** Nothing here changes firmware, `sdkconfig`, or an
accepted ADR. Evidence class: **static** (source reading and arithmetic). Hardware timing,
filter response, and signal quality: **NOT RUN / not measured.**

Decision owner: Project Owner. Reviewers and AI contributors give arguments and calculations;
they do not decide, and the author of this draft cannot be its independent reviewer.

Related: [ADR-0002](../adr/0002-fnirs-optical-acquisition-timing.md) (Accepted; section 3.8, R11,
Q-4), issue #67 (timing and K draft), issue #66 (analog/ADC profile), `docs/V4_IDF61_SINGLE_CLOCK.md`.

## 1. Question

Should the averaging count `K` (and `K_min`) stay fixed at 10 and 8 for every optical frame
rate `Fs`, or depend on `Fs` (and possibly on the OFF-phase split)? If it depends on `Fs`, what is
the rule, and what must be measured to justify it?

## 2. What exists today (verified in source at `8723e72`)

| Item | Value | Where |
|---|---|---|
| `K` definition | number of contiguous valid ADC samples averaged inside one LED phase window; not an ON:OFF ratio | ADR-0002 section 3.8 |
| `K_target`, `K_min` | 10 and 8, per ON phase and per OFF phase, fixed for all allowed `Fs` ("until an ADR amendment") | ADR-0002 R11, Q-4; `nocti_v4_scheduler_config.h` (`NOCTI_V4_K_ON_TARGET` 10, `NOCTI_V4_K_ON_MINIMUM` 8, same for OFF) |
| Geometry K minimum | 8 | `nocti_v4_sample_clock.h` (`NOCTI_V4_SAMPLE_K_MIN`) |
| ADC rate | 8000 SPS, 125 us period | `NOCTI_V4_SAMPLE_PERIOD_US` |
| Slots per frame | 20 | `NOCTI_V4_SAMPLE_SLOT_COUNT` |
| Settle / guard / lead | 4 / 1 / 2 samples (3 sinc3 periods plus 1 unmeasured front-end placeholder; guard quantized from 100 us; lead as in the single-clock doc) | `nocti_v4_sample_clock.h`, `V4_IDF61_SINGLE_CLOCK.md` |
| Stable windows | `on_stable = off_samples - settle`; `off_stable = slot - off_samples - settle - guard` | `nocti_v4_sample_geometry_for` |
| Mean computation | integer mean of the latest contiguous `K` samples per channel, `int64` sum, in the optical consumer (not in the DRDY ISR or ADS task) | `latest_contiguous_mean` in `nocti_v4_optical.c` |
| Firmware allowed `Fs` | 2, 4, 5, 8, 10 Hz | `nocti_v4_sample_fs_allowed` |
| Firmware default OFF phase | 44 % of slot (2200 us at 10 Hz); 50 % is the "qualification" phase | `nocti_v4_scheduler_config.h` |

## 3. Discrepancies to resolve first

1. **Phase split.** ADR-0002 locks the default ON/OFF split at 50/50. Firmware defaults to 44 %
   (the ON phase gets 44 % of the slot, the OFF window the rest, see formulas above). Which one is
   intended for the science profile?
2. **Allowed rates.** ADR-0002 lists {1, 2, 4, 5, 8, 10} Hz. Firmware accepts {2, 4, 5, 8, 10}
   (1 Hz is not allowed in `nocti_v4_sample_fs_allowed`). Is 1 Hz still required?
3. **Fixed vs scaled K.** ADR-0002 says fixed 10/8 for all rates. This draft asks whether that
   should change; doing so needs an ADR amendment, not a code-only change.

## 4. Stable-sample capacity per rate (arithmetic, not measured)

`slot_samples = 8000 / (20 * Fs) = 400 / Fs`. Quantization: `round(us / 125)` using the firmware
rule `(us + 62) / 125`. Settle 4, guard 1. Counts below are samples available **before** any
late-commit shift (a late commit moves the stable window later and is counted, see single-clock doc).

| Fs (Hz) | slot (us) | slot samples | ON interval samples, 44 % | ON stable, 44 % | OFF stable, 44 % | ON interval samples, 50 % | ON stable, 50 % | OFF stable, 50 % |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 (not in firmware) | 50000 | 400 | 176 | 172 | 219 | 200 | 196 | 195 |
| 2 | 25000 | 200 | 88 | 84 | 107 | 100 | 96 | 95 |
| 4 | 12500 | 100 | 44 | 40 | 51 | 50 | 46 | 45 |
| 5 | 10000 | 80 | 35 | 31 | 40 | 40 | 36 | 35 |
| 8 | 6250 | 50 | 22 | 18 | 23 | 25 | 21 | 20 |
| 10 | 5000 | 40 | 18 | 14 | 17 | 20 | 16 | 15 |

Columns "ON interval samples" are the firmware's `off_samples`: the length of the first (ON)
interval, after which the OFF commit happens. The code name is misleading; the meaning follows the formulas. Check against `V4_IDF61_SINGLE_CLOCK.md` at 10 Hz,
44 %: 18 samples, ON stable 14, OFF stable 17.

Reading the table:

- At every listed rate and both splits, both windows hold at least `K_target = 10` samples. At
  10 Hz the margin is smallest: ON stable 14 (44 %) or 15-16 (50 %) versus `K = 10`.
- At 2 Hz and 4 Hz the windows are 4-10 times larger than `K = 10`, so a larger `K` is feasible.
- Whether a larger `K` is **useful** depends on noise behavior, which is not in this repository.

## 5. Computation cost

Mean over `K` samples per channel is a sum and one division per window. At 4 channels, 20 slots,
10 Hz: 1600 divisions per second plus 32000 additions per second (estimate; `cpu` is
`not_measured` in `get_timing`). The cost is negligible next to the ADS service budget
(`ads_budget_us` 80, bench `service_max` about 73 us); the constraint is **where** it runs
(consumer task only, never in the DRDY ISR or ADS task), not how much.

## 6. Open questions (each needs a source or a measurement)

1. **Noise model.** Are consecutive 8 kSPS samples (sinc3, OSR from the ADS131M04 configuration)
   independent enough that noise falls like `1/sqrt(K)`? Samples inside one window are correlated
   by the filter. Cite the datasheet equation or a measurement.
2. **Dominant error.** Is the error budget limited by white ADC noise, by LED drift, by ambient
   light changes, or by settling error? If not white noise, a larger `K` helps little.
3. **Settling.** The front-end settling term is an unmeasured placeholder (1 sample). Issue #66.
   A longer analog settle shrinks the stable window and changes the feasible `K` at 10 Hz.
4. **Scaling rule candidates** (propose and compare):
   - (a) Fixed `K = 10`, `K_min = 8` for all `Fs` (current ADR).
   - (b) `K` proportional to the stable window, capped (for example `K = min(Kmax, floor(stable * f))`).
   - (c) A small table per supported `Fs`, chosen from measurement.
   - (d) Fixed `K` for data, but report the achievable window size so the host can average more.
5. **Insufficient data policy.** Current behavior marks the slot invalid when under `K_min`
   (reason `UNDER_K_ON` or the OFF equivalent). Keep it? Partial result with a flag?
6. **ON/OFF symmetry.** ADR-0002 requires equal `K` on both phases for `I_ON - I_OFF`. With the
   44 % split the windows are unequal (14 vs 17 at 10 Hz). Is equal `K` still required if windows differ?
7. **Latency and heating.** Larger `K` at low `Fs` does not change LED duty. Does it change anything
   a user sees (frame latency is set by `Fs`, not by `K`)?
8. **Late-commit shift.** How much margin should `K_min` leave so one late commit does not push
   the window below `K_min` at 10 Hz?

## 7. What a good answer looks like

For each proposal, give: the formula for `K` and `K_min` as a function of `Fs` and split; the
resulting table for Fs 2, 4, 5, 8, 10 (and 1 if retained); the source of every noise or settling
number (datasheet section, measurement, or "assumption"); and what test or bench measurement
would confirm it. Do not present a calculation as a measurement. Do not adopt the historical
10/8 or any new value without a stated reason. Disagreements stay visible; do not average opinions.

## 8. Not in scope

Firmware changes, scheduler changes, `sdkconfig`, codec or protocol changes, LED current, RAW
buffer work (Batch 4b), merges, flashing, or any hardware operation.

## 9. Decision record (fill in after discussion)

| Item | Decision | Date | Approver |
|---|---|---|---|
| Phase split for science profile | pending | | Owner |
| Allowed `Fs` set | pending | | Owner |
| `K` policy (fixed / scaled / table) | pending | | Owner |
| `K_min` policy | pending | | Owner |
| ADR-0002 amendment needed | pending | | Owner |
