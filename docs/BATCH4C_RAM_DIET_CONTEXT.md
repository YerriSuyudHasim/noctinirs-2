# Batch 4c: internal RAM reduction and Wi-Fi TX buffer count (context for a new session)

Purpose: hand-over document so a fresh chat or agent can start Batch 4c without the earlier
conversation. Read it fully before changing anything. Evidence tags: `[bench]` Owner bench,
`[elf]` read from `NoctiNIRSv4_IDF61.elf` with `xtensa-esp32s3-elf-gdb ptype/o`, `[map]` linker map,
`[source]` read from source, `[hitung]` computed, `[asumsi]` assumption, `[tidak terverifikasi]` not verified.

Status: **PROPOSED. Nothing in 4c is implemented or approved yet.** ESP-IDF build, flash and
on-target measurement are done only by the Owner on the lab computer.

## 1. Owner and working rules

- Owner: Yerri (not an IT background). Talk to the Owner in simple Indonesian. Repository files,
  commits, issues and PRs are written in English. Stay focused on what is asked.
- Follow `AGENTS.md` in the repository root (cross-project policy in the private `ai-engineering-hub`).
  Workflow: issue -> branch -> pull request -> CI -> independent review -> human merge approval.
  An agent never merges, force-pushes, amends, signs, flashes, or opens a PR without the Owner's explicit
  instruction. The author of a change cannot be its independent reviewer.
- Never edit `reference/` (historical V2/V3/V4 snapshots). Run `tools/verify-reference.ps1` before/after.
- Changes that touch tasks, stacks, queues, ISRs, DMA, clocks, heap or real-time behavior need a
  specialist review (Hardware/RTOS) in addition to normal review.
- Classify evidence honestly: static, host/unit, simulation, on-target bench, HIL, field. Compilation
  does not prove timing or safety. Report what was NOT RUN.
- PSRAM must not be used on the ESP32-S3 board (pin conflict): no `CONFIG_SPIRAM`, no buffers in PSRAM.
- RAW packets and ENGINEERING frames are diagnostic data, not science data.

## 2. Where we are (end of Batch 4b)

- PR #92 (Batch 4b) was squash-merged by the Owner into `cursor/n3-single-clock-72a4` as `7bcc01d` on 2026-11-06.
  Batch 4c works on branch `feat/n4c-ram-reduction`, created from `7bcc01d`, and its PR targets
  `cursor/n3-single-clock-72a4` (not `main`: the V4 firmware is not on `main`, which is 34+ commits behind). The older
  branch `feat/n4c-ram-diet` (based on the pre-merge 4b head) is obsolete; do not use it.
- Reviews of 4b: independent review (Codex) ended as COMMENT after earlier REQUEST CHANGES; RTOS specialist review
  (Grok) COMMENT, no blocking defects. Host tests at merge: 147, 0 failures. CI was green on the PR head.
- Follow-ups left open by those reviews (take them into 4c only if the Owner agrees):
  1. Wording of the `lost_in_breaks / 40` hint in `docs/BATCH4B_RAW_HEAP_MARGIN.md` (section 8 criterion text): it is a
     consistency check that assumes 40-sample packets (received mean 40.0003), not proof.
  2. A host test for epoch change / sequence gap while older entries are still queued across the ring wrap.
  3. A two-thread stress test of the SPSC ring with a sanitizer (not done; host tests are single-threaded).
  4. The Batch 4b bench evidence is a summary of pasted terminal text (`docs/evidence/batch4b-bench-2026-11-06.md`);
     the original log is not archived and the board revision / actual `sdkconfig` were not recorded. Record both in 4c benches.
- Batch 4b result: ENGINEERING RAW buffer 128 -> 96 samples, `sizeof(nn2_raw_buffer_t)` 5216 B.
  Heap at ENGINEERING: `heap_free_min` 6448 B (4a: 3068 B). ACQ: no regression.
- Open problem 4b left: in ENGINEERING RAW capture, 110 `sendto` failures with errno 12 (ENOMEM) in 60 s,
  about 0.92 % of packets (4a: 22 and 38). `lost_in_breaks / 40 == frame_send_errors` in 3 of 3 captures.
  `heap_free_min` stayed at 6448 B while failing, so **total free heap is not the cause**. Best-supported
  cause: the Wi-Fi TX buffer count `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=4` (`sdkconfig.defaults` line 60,
  comment "about 20 kB/s to one station"). RAW sends about 200 packets/s, about 120 kB/s (6 times the design
  contract). ESP-IDF Kconfig help: UDP senders can run out of TX buffers; `esp_wifi_internal_tx` documents
  `ESP_ERR_NO_MEM`, mapped to errno 12 in `wlanif.c` `[source]`. **Not proven on target** `[tidak terverifikasi]`.
- Owner decision in 4b: accept about 1 % RAW loss in ENGINEERING, no `sdkconfig` change in 4b. 4c is where the
  hard limits of 4b may be relaxed, only with the Owner's explicit approval per item.
- Batch 4c is not about K. K stays `K_target=10`, `K_min=8` (ADR-0002, Owner confirmed). Do not reopen it here.

## 3. Goal of Batch 4c

1. Free internal RAM safely (static `.bss` first), so the heap has room.
2. With that room, raise the Wi-Fi TX buffer count (4 -> 6 or 8) and check whether `frame_send_errors` drops
   in ENGINEERING RAW capture, while `heap_free_min >= 4096 B` and ACQ stays unchanged.
3. Keep the ADS/DRDY path and timing exactly as qualified. No new work in the DRDY ISR or ADS task.

Non-goals: K selection, PSRAM, send retry, protocol changes, OPT2/SESSION_REPORT/gate work, the 4a small
findings (`i2c_on/off_us` 130/66 pattern, `sched_error -1`, `INVALID_STATE` while STREAMING for `netdiag`,
`net_hwm_words`, test mirror struct in `test_v4_batch1`) unless the Owner asks.

## 4. Measured facts

### 4.1 Memory map (map file, 4b build) `[map]`

| Region | Size | Note |
|---|---:|---|
| `.iram0.text` + vectors + end | about 147,700 B | includes Wi-Fi IRAM functions |
| `.dram0.data` | 25,192 B | gated in `config/ram_budget.json` |
| `.dram0.bss` | 172,376 B | gate UNSET, measured |
| DRAM segment seen by the linker | 341,760 B | shares physical SRAM with IRAM |

Chip: ESP32-S3 has 512 KB internal SRAM (general knowledge, not from this repo).

### 4.2 Heap snapshots `[bench]`

| Point | Free heap |
|---|---:|
| `before_wifi_init` | 79,496 B (largest 31,744) |
| `after_wifi_start` | 28,228 B (largest 19,456) |
| ACQ idle, client connected | about 18,600 B (largest 7,680) |
| ENGINEERING idle | 13,196 B |
| `heap_free_min`, ACQ 600 s | 10,076 B |
| `heap_free_min`, ENGINEERING 60 s capture | 6,448 B |

Wi-Fi + lwIP init takes 51,268 B. `nn2_net` task: stack 16,384 B + TCB 340 B. Static Wi-Fi RX buffers 4 x 1,600 B.
Do not read more into the snapshots than this table; the exact order of allocations vs. the first snapshot is not itemized.

### 4.3 Largest static objects `[elf]`

`optical_runtime_t` 88,840 B (`nocti_v4_optical.c`, single `s_runtime`):

| Member | Size | Comment |
|---|---:|---|
| `work[3][20]` (`slot_work_t` 672 B each) | 40,320 | history 3 frames x 20 slots |
| `raw_stage[256]` (`nocti_v4_raw_sample_t` 56 B) | 14,336 | optical staging of raw samples |
| `finalized_timing[32]` (`nocti_v4_slot_timing_t` 304 B) | 9,728 | |
| `optical_stack` 6,144 + `frame_stack` 4,096 | 10,240 | static task stacks |
| `builder` 2,864 + `frame_ring[2]` 5,696 + `latest_frame` 2,848 | 11,408 | `nocti_v4_completed_optical_frame_t` 2,848 B |
| `anomaly[32]` 1,024; `mismatch[16]` 256; `phase_stats[8]` 384; `phase_builder[8]` 160 | 1,824 | diagnostics |
| inside `work`: `phase_candidate_on/off[8]` 16 B and `v5_candidate_*` 8 B per slot | about 1,440 | V5 sweep diagnostics |

`scheduler_runtime_t` 36,152 B: `slots[3][20]` 18,240; `finalized_ring[32]` 9,728 (same type as optical's);
`task_stack[6144]` 6,144; `deadline_ring[8]` 448.
`ads_runtime_t` 18,360 B: `raw_ring[256]` 14,336; `event_ring[128]` 2,048; `ch0_selftest` 440; `frame_dump` 392.
`async_led_runtime_t` 8,104 B: `task_stack[4096]` 4,096; `timing[4][10]` 1,920; rings about 830 B.
Other registered: `s_nn2_app` 4,096 B; `s_status` 48 B; `s_raw` pointer 4 B.

No `float` or `double` exists in `components/`, `main/` or `firmware/v4-idf61` source (grep), so a float -> int
change saves nothing.

### 4.4 Task stacks (configured / measured free at the end of a 600 s ACQ run)

Units are bytes (the 16,384 B net stack cannot hold 4,476 four-byte words). Free = high-water mark `[bench]`.

| Task | Stack | Free | Used (hitung) |
|---|---:|---:|---:|
| ADS | 4,096 | 1,760 | 2,336 |
| LED | 4,096 | 1,708 | 2,388 |
| Scheduler | 6,144 | 4,704 | 1,440 |
| Optical | 6,144 | 4,812 | 1,332 |
| Frame | 4,096 | 588 | 3,508 (tight, do not shrink) |
| Dark | 4,096 | 3,272 | 824 |
| Control | 8,192 | 5,724 | 2,468 |
| Net | 16,384 | 4,476 | 11,908 |

Free values come from one run in normal ACQ; they are not worst-case figures `[asumsi]`.

### 4.5 Timing reference (ACQ, 10 Hz, 600 s, Batch 4b bench) `[bench]`

`fault/edge_miss/adc_missed/gap/crc_bad/spi_errors = 0`, `spi_max` 59 us, `service_max` 73, `lag_max` 31,
`late_max` 2, `sched_deferred_max` 254, `raw_depth_max` 19-23 (this is the ADS `raw_ring` depth,
`nocti_v4_ads.c`; the optical `raw_stage_depth_max` is not exposed on the wire `[source]`).

## 5. Candidate changes (ranked by risk). Each needs the Owner's approval before work starts.

| # | Change | Saves | Risk and gate |
|---|---|---:|---|
| 1 | `raw_ring` (ADS) 256 -> 128 and `raw_stage` (optical) 256 -> 128 | 14,336 B | Measured depth 19-23 vs 256. 128 entries = 16 ms at 8 kSPS. Stall > 16 ms loses samples (counted by existing counters). Must check how the ADS ring overflow is handled and that nothing else assumes 256. Touches the ADS data path: needs Hardware/RTOS review and Bench A. |
| 2 | Remove unused diagnostics (V5 sweep `phase_*`, `anomaly`, `mismatch`, `ch0_selftest`, `frame_dump`) | about 4,100 B + code | First list which are read by `get_health`/`get_timing`/tools/tests; remove only the unused. Wire keys must not change silently (golden/contract tests). |
| 3 | `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` 4 -> 6, then 8 (`sdkconfig.defaults`) | costs about 0.65 KB per buffer in use `[asumsi]` | `sdkconfig` change needs Owner approval. After editing `sdkconfig.defaults`, the lab machine must delete its local `sdkconfig` (or `idf.py fullclean`) so the new value is used. Not proven to remove the loss. |
| 4 | Static stacks: scheduler 6,144 -> 3,072, optical 6,144 -> 3,072, dark 4,096 -> 2,048, control 8,192 -> 4,096 (keep about 1 KB free over measured use) | 12,288 B | Stack overflow is fatal and the margins are from one normal run. Do not do this without worst-case stack measurement and specialist review. Frame stack stays. |
| 5 | History 3 frames -> 2 (`work`, scheduler `slots`) | 19,520 B | Changes finalization logic and timing. Only with a design review. |
| 6 | `finalized_timing`/`finalized_ring` 32 -> 16 | 9,728 B | Needed depth not measured. |

Proposed order: 1 and 2 first, then 3, bench, and only then decide on 4-6. Stop and ask the Owner if a change
needs a decision not listed here.

## 6. Hard limits that still apply unless the Owner lifts them per item

Do not change: timing constants (lead 2, settle 4, guard 1), task priorities/cores, I2C speed, pins, TWDT/watchdog,
partitions, codec (`nn2_core_optical_codec.*`), golden vectors, port 3334, `reference/`. No new dynamic allocation on the
data path (the RAW buffer stays allocated only on `set-mode ENGINEERING` while STOPPED). No send retry. No new
safety/gate features. Preserve reviewed lock manifests and SDK defaults not related to this batch.

## 7. Files to read first

`AGENTS.md`; `docs/BATCH4B_RAW_HEAP_MARGIN.md`; `docs/RAM_BUDGET.md`; `config/ram_budget.json`;
`tools/check_ram_budget.py`; `tests/architecture/test_ram_budget.py`; `firmware/v4-idf61/sdkconfig.defaults`;
`firmware/v4-idf61/components/nocti_v4_optical/nocti_v4_optical.c` (`optical_runtime_t`);
`firmware/v4-idf61/components/nocti_v4_ads/nocti_v4_ads.c` (`ads_runtime_t`, `raw_ring`);
`firmware/v4-idf61/components/nocti_v4_scheduler/` (`scheduler_runtime_t`, history constants);
`firmware/v4-idf61/components/nocti_v4_common/include/nocti_v4_rtos.h` (stack sizes);
`components/nn2_net/nn2_net.c` (frame send loop, 4-frame budget per round);
`docs/V4_IDF61_SINGLE_CLOCK.md`.

## 8. Verification required for each step

Run the CI command set on the exact commit and compare with the base commit (`feat/n4b-raw-heap-margin` head):

```text
python tools/run_host_tests.py --build <new temp dir> --evidence <new temp dir> --failure-controls
python tools/verify-v4-idf61.py
python -m unittest discover -s tests/architecture -v
ctest --test-dir <build dir> --output-on-failure
```

Update `config/ram_budget.json`, `docs/RAM_BUDGET.md` and the `migration-manifest.json` hashes (only what
`verify-v4-idf61.py` requires) whenever a registered symbol or shared file changes. Wait for GitHub Actions on the
newest commit and report job names and results. A local machine may lack a native C compiler; report the real
result or NOT RUN, never an invented one. Edits to existing static structs can be checked for size with a compile-only
probe using the Xtensa ESP32-S3 compiler (`_Static_assert(sizeof(...))`).

### Owner bench steps (lab computer, after pulling the branch)

```text
git fetch origin ; git checkout feat/n4c-ram-reduction ; git log -1 --oneline ; git status --short
idf.py -C firmware\v4-idf61 -p COM11 build flash
idf.py -C firmware\v4-idf61 -p COM11 monitor
python tools/check_ram_budget.py firmware\v4-idf61\build\NoctiNIRSv4_IDF61.map
python tools/nn2ctl.py info ; python tools/nn2ctl.py status ; python tools/nn2ctl.py health ; python tools/nn2ctl.py timing ; python tools/nn2ctl.py netdiag
```

If `sdkconfig.defaults` changed, delete the local `firmware\v4-idf61\sdkconfig` first. Bench A (ACQ):
`python tools/nn2ctl.py run --rate 10 --seconds 600 --led`, then `timing`, `health`, `netdiag` while STOPPED
(`netdiag` returns `INVALID_STATE` while STREAMING). Bench B (ENGINEERING RAW, repeat twice):
`python tools/nn2ctl.py set-mode --mode ENGINEERING`; `netdiag`; `python tools/nn2_raw_capture.py --seconds 60 --rate 10 --output <path outside the repo>`;
`netdiag`; `health`; `python tools/nn2ctl.py set-mode --mode ACQ`; `netdiag`. Do not commit CSV or logs.

Pass criteria for 4c: ACQ equals the reference in 4.5; ENGINEERING: `heap_free_min >= 4096 B` (proposed), no leak
after `set-mode ACQ` (about 18.5 KB or more), `raw_dropped_samples == 0`, and `frame_send_errors`
compared with the 4b run (110 per 60 s) with `lost_in_breaks / 40 == delta(frame_send_errors)`. A lower
`frame_send_errors` after raising the TX buffer count supports the TX-buffer hypothesis; no change contradicts it.

## 9. Future board note (not part of 4c)

Board 2 is planned with an ESP32-P4 (768 KB internal SRAM per ESP-IDF `soc.h`, PSRAM 32 MB) and an ESP32-C6 mini as a
Wi-Fi co-processor (ESP-Hosted). The Wi-Fi TX path and buffer limits there differ and must be measured separately.
Keep ADC/ISR-critical buffers in internal SRAM.

## 10. Open questions for the Owner

1. Approve change 1 (ring sizes 256 -> 128) and change 2 (remove unused diagnostics)?
2. Approve `sdkconfig.defaults` `DYNAMIC_TX_BUFFER_NUM` 6 or 8 for the experiment?
3. Are stack reductions (change 4) wanted at all, given the worst-case measurement needed first?
4. Is the 1 % ENGINEERING RAW loss still acceptable if 4c shows no improvement?
