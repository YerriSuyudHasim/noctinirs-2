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

The 4b bench ran at 10 Hz only, so its `raw_depth_max` (23 in ACQ, 19 in ENGINEERING; the ADS ring, not the optical
stage) says nothing about 2 and 4 Hz. Capacity must be a power of two (`RAW_STAGE_MASK`), so 128 is the next step
down and it breaks the budget at 2 and 4 Hz. Overflow of the stage sets `staging_integrity_fault`.
Options for the Owner: keep 256 (no saving); drop 2 and 4 Hz (a protocol change); or non-power-of-two indexing
(saves little, as in 4b). Nothing was changed for 1b, and the verifier was not edited.
RTOS review of PR #93 agrees and recommends keeping 256. It adds that 256 leaves only 23 records of headroom at
2 Hz (233 needed), so this stage is tighter than the ADS ring. Larger redesigns (a smaller staged record, or
classifying samples into slot work as they arrive) would need their own design review and parity/golden tests.

## 3. Risks of what was changed

- 1a: ring overflow in the ADS task is a hard fault (`set_fault`, `NOCTI_V4_ADS_FAULT_RAW_RING_OVERFLOW`): DRDY is
  disabled and the session ends. 128 records are 16 ms at 8 kSPS (computed capacity). The only measured depths come
  from the 4b bench on the previous 256-record build, at 10 Hz only: 23 records in ACQ (600 s) and 19 in an
  ENGINEERING 60 s capture, that is about 2.4-2.9 ms. The 128-record build has not been benched and 2, 4, 5 and 8 Hz
  were never measured. RTOS review (R1): the optical task classifies a whole slot in one burst (40 samples at 10 Hz,
  100 at 4 Hz, 200 at 2 Hz), which may push the ring toward 128 at low rates. This is a calculation plus an
  assumption, not a measurement. It is the gate for merging 1a.
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

Use the steps in `BATCH4C_RAM_DIET_CONTEXT.md` section 8. Delete the local `firmware\v4-idf61\sdkconfig` first
(`idf.py fullclean` is not enough, it keeps `sdkconfig`; the lab note in the `f28a2c1` commit message that
mentions it is superseded by this text). To attribute effects, bench the commits separately: `fed718d` (RAM only,
also a control for fragmentation), then `f28a2c1` (TX 6). Save the full terminal log, the board revision and the
`sdkconfig` used. After each build, check that `build/config/sdkconfig.h` shows
`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM 6` (on `f28a2c1`) and `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER 1`, and archive it.

Measurements proposed by the RTOS review (the pass limit of 64 is its opinion, the Owner may change it):

1. ACQ at 2, 4 and 10 Hz (`nn2ctl.py run --rate <n> --seconds 600 --led`), then `timing` and `health`:
   `raw_depth_max` (proposed pass: at most 64 at every rate), `adc_ring_overflow == 0`, no ADS fault. Keep the serial
   lines `OPTICAL_EXEC` (`batch_duration_max_us`, `classify_max_us`, `batch_samples_max`) and
   `FRAME_PRECOMMIT ... stage_depth_max= stage_overflow=` (also the input for the 1b decision).
2. ENGINEERING RAW at 10 Hz and at 2 Hz, 60 s, twice each, on `f28a2c1`: `raw_depth_max`, `raw_dropped_samples`,
   `frame_send_errors`, `frame_last_errno`, `heap_free_min`, `largest_free_block_now` before and after.
3. Heap baseline on `fed718d`: `NN2_HEAP before_wifi_init` / `after_wifi_start` against 4b (79,496 / 28,228 B);
   about 7 KB more is expected. Run `tools/check_ram_budget.py` on the map for `nocti_v4_ads.s_runtime` = 11,192 B.
4. If DARK_DIAGNOSTIC mode is used, one run with `raw_depth_max` recorded.

Other pass criteria are in the context document section 8 (`heap_free_min >= 4096 B`, ACQ equal to the 4b reference,
no fault, `raw_dropped_samples == 0`, `lost_in_breaks / 40 == delta(frame_send_errors)`).

## 6. Review findings (PR #93, head `94350bc`)

| Finding | Status |
|---|---|
| Codex P2: `idf.py fullclean` keeps `sdkconfig`, TX=6 could silently run as TX=4 | Fixed in the context document and section 5. The `f28a2c1` commit message is not rewritten; section 5 supersedes it. |
| Codex P3: 6,448 B heap minimum does not rule out fragmentation | Context document wording corrected. Fragmentation stays open. |
| Codex P3 and RTOS R5: depth 19-23 was measured on the 256-record build, as two runs (23 ACQ, 19 ENGINEERING) | Corrected in section 3, section 2, and the `nocti_v4_ads_config.h` comment. |
| RTOS R1/R2/R3: low-rate classify burst, core-0 preemption, DARK_DIAGNOSTIC | Open. Needs the measurements in section 5. Gate for merging 1a. |
| RTOS R4: heap and fragmentation from two more TX buffers | Open, low risk. Measure in section 5. |
| RTOS note: comment in `nocti_v4_ads_config.h` says "16-deep" while `nocti_v4_backlog.h` asserts 8 | Pre-existing, not from this PR. Not changed. |
