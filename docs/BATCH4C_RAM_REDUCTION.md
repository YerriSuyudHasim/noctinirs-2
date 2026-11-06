# Batch 4c: internal RAM reduction and Wi-Fi TX buffers (work record)

Base: `6ad68fd` (4b merged as `7bcc01d`). Branch: `feat/n4c-ram-reduction`. The PR will target
`cursor/n3-single-clock-72a4`. Context and candidate list: `BATCH4C_RAM_DIET_CONTEXT.md`.
Evidence classes: **static**, **host/unit**, **on-target bench** (Owner only). Sizes marked `[hitung]` are computed, not measured.

## 1. Status

| Item | Commit | Status |
|---|---|---|
| 1a ADS `raw_ring` 256 -> 128 records (-7,168 B `.bss`) | `fed718d` | Done in source. Owner approved 2026-11-06 (chat). |
| 1b optical `raw_stage` 256 -> 128 | none | **Blocked**, see section 2. Needs an Owner decision. |
| 2 remove unused diagnostics | none | Not started. Not approved. |
| 3 `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` 4 -> 6 | `f28a2c1` | Done in source. Owner approved 4 -> 6 only. 8 is NOT done. |
| 4, 5, 6 stacks, history, finalized rings | none | Not started. Not approved. |

## 2. Why 1b is blocked

`tools/verify-v4-idf61.py` (lines 70-75) requires `RAW_STAGE_CAPACITY 256u` and checks the design budget
`slot // 125 + 32 + 1 <= 256` for every supported frame rate. The supported acquisition rates are exactly
2, 4, 5, 8 and 10 Hz (`docs/adr/0007-ads131-acquisition.md`, `docs/V4_IDF61_SINGLE_CLOCK.md`, `tools/nn2ctl.py`).
The raw sample period is 125 us, a slot is 1,000,000 / (20 x Fs) us.

| Fs (Hz) | Slot (us) | Samples per slot | Slot + batch 32 + 1 | Fits 256 | Fits 128 |
|---:|---:|---:|---:|:--:|:--:|
| 2 | 25,000 | 200 | 233 | yes | **no** |
| 4 | 12,500 | 100 | 133 | yes | **no** |
| 5 | 10,000 | 80 | 113 | yes | yes |
| 8 | 6,250 | 50 | 83 | yes | yes |
| 10 | 5,000 | 40 | 73 | yes | yes |

The 4b and 4c benches run at 10 Hz only, so a measured `raw_depth_max` of 19-23 (the ADS ring, not the optical
stage) says nothing about 2 and 4 Hz. Capacity must be a power of two (`RAW_STAGE_MASK`), so 128 is the next step
down and it breaks the budget at 2 and 4 Hz. Overflow of the stage sets `staging_integrity_fault`.
Options for the Owner: keep 256 (no saving); drop 2 and 4 Hz (a protocol change); or non-power-of-two indexing
(saves little, as in 4b). Nothing was changed for 1b, and the verifier was not edited.

## 3. Risks of what was changed

- 1a: ring overflow in the ADS task is a hard fault (`set_fault`, `NOCTI_V4_ADS_FAULT_RAW_RING_OVERFLOW`): DRDY is
  disabled and the session ends. Measured maximum depth was 19-23 of 128 in one 600 s ACQ run at 10 Hz (about
  16 ms of backlog at 8 kSPS). That is not a worst case. Needs Hardware/RTOS review.
- 3: the cost of one extra TX buffer is unverified (0.65 KB in the 4c context, up to 1.5 KB in the 4b analysis). Worst
  case for two buffers is about 3 KB, covered on paper by the 7,168 B freed in 1a. The effect on ACQ is an assumption.
  Whether the TX buffer count is the real limit is unproven. Needs sdkconfig/heap review.

## 4. Verification record (host: Linux cloud session, GCC; not the Owner's lab computer)

| Check | Base `6ad68fd` | `fed718d` | `f28a2c1` |
|---|---|---|---|
| `run_host_tests.py --failure-controls` | 147 tests, 0 failures, exit 0 | same | same |
| `verify-v4-idf61.py` | PASS | PASS (after manifest hash update) | PASS |
| `unittest discover tests/architecture` | 163 OK (1 skipped, 2 expected failures) | same | same |
| `ctest` | 2/2 passed | 2/2 passed | 2/2 passed |
| ESP-IDF build, `sizeof` probe, linker map, flash, bench | NOT RUN | NOT RUN | NOT RUN |

NOT RUN: no Xtensa compiler or ESP-IDF here, so the 11,192 B size of `nocti_v4_ads.s_runtime` is computed
(18,360 - 7,168), not measured. `tools/verify-reference.ps1` was not run (no PowerShell); `git status` shows no
change under `reference/`.

## 5. Owner bench plan (lab computer)

Use the steps in `BATCH4C_RAM_DIET_CONTEXT.md` section 8. Delete the local `firmware\v4-idf61\sdkconfig` first.
To attribute effects, bench the commits separately: `fed718d` (RAM only, also a control for fragmentation), then
`f28a2c1` (TX 6). Save the full terminal log, the board revision and the `sdkconfig` used. Also keep the serial line
`FRAME_PRECOMMIT ... stage_depth_max=` from an ACQ run (at 2, 4 and 10 Hz if possible) for the 1b decision.
Pass criteria are in the context document section 8 (`heap_free_min >= 4096 B`, ACQ equal to the 4b reference, no
fault, `raw_dropped_samples == 0`, `lost_in_breaks / 40 == delta(frame_send_errors)`).
