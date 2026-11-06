# Batch 4c bench record (key fields only)

Source: terminal output pasted by the Owner, 2026-11-06 (Asia/Seoul evening). Target ESP32-S3 (COM11), SoftAP, `info`:
`baseline=V4_IDF61_NN2_CTRL; short_policy=STRICT`. Per-slot arrays (`async_i2c`, `sync.slots`) are omitted.
Class: on-target bench (Owner). Not independently reproduced.

Builds:
- **Build A**: Owner reported commit `fed718d` (ADS `raw_ring` 256 -> 128, TX buffers 4). The `git log` output, `info` line and `A-sdkconfig.h`
  content of Build A were not pasted to the reviewer, so its commit and TX=4 setting are the Owner's statement, not shown here. `boot_id` 171.
- **Build B**: commit `6c65094` (adds TX buffers 4 -> 6; later commits are documentation only). `sdkconfig.h`:
  `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER 1`, `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM 6`. `tools/check_ram_budget.py` PASS,
  `nocti_v4_ads.s_runtime` = 11,192 / 11,192 B (measured from the linker map). `boot_id` 176, then 179 and 180 (see below).
- Board hardware revision and the full terminal logs (`idf.py build flash`, boot `NN2_HEAP` lines) were **not** recorded or pasted.

An earlier flash of Build B produced an image with `baseline=V4_PIPELINE` (root firmware); the cause was not established. All B results
below were taken after `info` showed `V4_IDF61_NN2_CTRL`.

## RAM
`free_heap_before_wifi` 86,664 and `free_heap_after_wifi` 35,376 on both builds, against 79,496 and 28,228 in 4b: +7,168 B and +7,148 B,
equal to the ring reduction. `heap_free_now` while idle in ENGINEERING 20,332 (A) and 20,340 (B), so two more TX buffers cost no visible idle heap.

## ACQ, 30 s each (`run --rate <n> --seconds 30 --led`), Build B
| Rate | `adc_ok` | `raw_depth_max` | `adc_ring_overflow` | `fault` | `heap_free_min` |
|---:|---:|---:|---:|---|---:|
| 2 Hz | 237334 | 28 | 0 | none | 16876 |
| 4 Hz | 237285 | 23 | 0 | none | 16876 |
| 10 Hz, run 1 (boot 176, after 2 and 4 Hz, no reset) | 237288 | 24 | 0 | **FAULT_OPTICAL 267 at stop** | 16656 |
| 10 Hz, after reset (boot 179), 3 runs | 237293 / 237292 / 237288 | 26 / 16 / 19 | 0 | none | n/a |
All runs: `adc_missed` 0, `adc_sequence_gap` 0, `crc_bad` 0, `spi_errors` 0, `led_completion_timeout` 0, `late_max` 1-2.
Merge condition for 1a (`raw_depth_max` at most 64 at 2, 4 and 10 Hz): met on these runs (highest 28).
Build A ACQ, 10 Hz, 30 s (`boot_id` 171): `raw_depth_max` 15, `heap_free_min` 16,656, no fault. A was not run at 2 or 4 Hz.

## ENGINEERING RAW, 10 Hz, 30 s (`nn2_raw_capture.py --seconds 30 --rate 10`)
| Run | packets | `lost_in_breaks` | breaks | added `frame_send_errors` | `raw_depth_max` | `raw_buffer_highwater` | `heap_free_min` |
|---|---:|---:|---:|---:|---:|---:|---:|
| A raw1 | 5927 | 200 | 3 | 5 | 17 | 62 | 9948 |
| A raw2 | 5909 | 920 | 9 | 23 | 15 | 80 | 9948 |
| B raw1 | 5926 | 240 | 3 | 6 | 21 | 62 | 11680 |
| B raw2 | 5931 | 80 | 1 | 2 | 23 | 60 | 11348 |
| B raw3 (boot 180) | 5931 | 40 | 1 | n/a (no netdiag) | 22 | 55 | n/a |
| B raw4 | 5930 | 80 | 1 | 2 | 18 | 80 | 11496 |
| B raw5 | 5930 | 80 | 1 | 2 | 20 | 62 | 11276 |
`raw_dropped_samples` 0 in every run; `frame_last_errno` 12, `frame_send_eagain` 0, `frame_send_enobufs` 0; `lost_in_breaks / 40` equals the added
`frame_send_errors` wherever both were available. B raw1 baseline of `frame_send_errors` taken as 0 (earlier ACQ netdiag in the same boot).

Totals: A 28 failed sends in 11,836 packets (0.24 %, 2 runs); B 13 in 29,648 (0.04 %, 5 runs; raw3 counted from `lost_in_breaks`).
Run-to-run spread is large (A: 5 and 23; B: 6, 2, 1, 2, 2) and the first run of each build is nearly equal (5 and 6). Two A runs are too few to
attribute the difference to the TX buffer count. The hypothesis that the TX buffer count limits the loss is **not proven**. TX 8 was not tried.

## Open observations (recorded, not explained; Owner decided not to re-test on Build A)
1. **FAULT_OPTICAL (267)** once, at the stop of the first 10 Hz ACQ run on Build B (boot 176), after 2 and 4 Hz runs in the same boot and with
   no serial monitor open. `health` then showed `adc_ok` 237288, no ADS fault, `sched_error` 0, `raw_depth_max` 24. In firmware, `scan_stop()` can
   return an error from the scheduler, ADS, optical or async LED stop and the specific error is not exposed. `clear-fault` was refused
   (`PRECONDITION_FAILED`; by design a fault needs a reboot). Not reproduced in 3 later 10 Hz runs (after reset, monitor open) nor in 5 ENGINEERING runs.
   The cause is unknown; the Owner suspects the missing serial monitor, which is a guess.
2. **`sched_error` = 259** (`ESP_ERR_INVALID_STATE`) once, in `health` after B raw2 (boot 179), with state STOPPED and `fault` 0. The board was then
   reset (`boot_id` 180); `sched_error` was 0 after raw3, raw4 and raw5. The 4b bench also recorded a non-zero `sched_error` (-1) after an ENGINEERING capture
   ("known 4a finding", listed as a non-goal in `BATCH4C_RAM_DIET_CONTEXT.md`), so a non-zero value after ENGINEERING is not new in kind, but 259 is a
   different value and its source was not traced.
3. `late_2` is higher in ENGINEERING (124-282 in B, 66-106 in A; `late_max` 2 in both).
4. `largest_free_block_now` 7,680 B after ENGINEERING captures on B (10,240 before), the same value as the 4b bench.

## Not measured
Build A at 2 and 4 Hz; ACQ 600 s; DARK_DIAGNOSTIC; TX 8; per-buffer TX cost; boot `NN2_HEAP` lines; hardware revision; the original logs.
The Owner's remark "Build B is safe" (chat, 2026-11-06) is his decision; this record does not independently establish it.
