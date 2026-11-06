# Batch 4b: RAW buffer heap margin

Base: `8723e72` (merge of PR #90, Batch 4a). Branch: `feat/n4b-raw-heap-margin`.
Scope: explain the sporadic `sendto` failure `errno 12 (ENOMEM)` seen in ENGINEERING RAW capture,
and shrink the lazily allocated RAW buffer (option A). Nothing else changes.

Evidence classes used here: **static** (source reading, compile-only probe), **host/unit**
(native tests), and **on-target bench** (Owner, section 8, one ENGINEERING capture round). Sections 1-7 were
written before the bench; where they give a heap figure for the new build it is an estimate `[hitung]`, and
section 8 holds the measured values. Not done: second ENGINEERING round, long-duration RAW, hardware timing
beyond the figures in section 8. Bench raw outputs: `docs/evidence/batch4b-bench-2026-11-06.md`.

Source tags: `[bench]` Owner Batch 4a bench, `[sumber]` read from source, `[probe]` compile-only
size probe (Xtensa GCC 15.2, `xtensa-esp32s3-elf-gcc -fsyntax-only`), `[hitung]` computed here.

## 1. Files changed

| File | Function / item | Reason |
|---|---|---|
| `components/nn2_core/include/nn2_core_raw_buffer.h` | `NN2_RAW_BUFFER_CAPACITY` 128 -> 96; new `NN2_RAW_BUFFER_MIN_SAMPLES` 80; `NN2_RAW_BUFFER_MAX_BYTES` 6272 -> 5216; `_Static_assert` minimum 128 -> `>= MIN_SAMPLES` | Option A. |
| `components/nn2_core/src/nn2_core_raw_buffer.c` | `nn2_raw_buffer_push`, `nn2_raw_buffer_encode`; new static helpers `advance`, `distance`, `index_of`; removed `MASK` | 96 is not a power of two, so `x & MASK` cannot be used. See section 3.2. |
| `config/ram_budget.json` | `heap.raw_buffer_heap_bytes`, `compile_time_checks`, `s_raw` source text | Match the new constants. Heap margins stay `PROPOSED`. |
| `docs/RAM_BUDGET.md` | sections 2, 4, 5 | Same. |
| `firmware/v4-idf61/migration-manifest.json` | `shared_communication` sha256 of the two `nn2_core_raw_buffer.*` files | `tools/verify-v4-idf61.py` requires it (hashes of shared files). Only those two hashes changed. |
| `test/host/test_raw_engineering.c` | `mode_and_raw_sequence`, `full_buffer_gap_and_codec_limits` adapted; new `wrap_keeps_order_and_depth`, `two_slots_of_40_fit_without_drop` | Tests for the new capacity. |
| `tests/architecture/test_ram_budget.py` | expected `NN2_RAW_BUFFER_MAX_BYTES` 6272 -> 5216 | Follows the constant. |

Not touched: codec, golden vectors, timing constants, task priorities and stacks (including
`NOCTI_V4_RTOS_STACK_NET_BYTES`), I2C, pins, watchdog, partitions, `sdkconfig`, port 3334, `reference/`,
the ISR and the ADS task. `nn2_raw_buffer_push` is called from the optical consumer (not the DRDY
ISR, not the ADS task); its work per sample is the same plus a few integer compares and one add/subtract per access (see section 6).

## 2. E1 - cause of `ENOMEM`

### 2.1 Where errno 12 comes from (ESP-IDF v6.1, `C:\esp\v6.1\esp-idf`, tag `v6.1`) `[sumber]`

`nn2_net.c` calls `sendto(..., MSG_DONTWAIT, ...)` (frame loop). Path inside the stack:

1. `components/lwip/lwip/src/api/sockets.c`, `lwip_sendto()`: `LWIP_NETIF_TX_SINGLE_PBUF` is 1 in
   `components/lwip/port/include/lwipopts.h:765`, so the function calls `netbuf_alloc(&buf, short_size)`
   and copies the datagram. If it returns NULL, `err = ERR_MEM` (sockets.c:1691-1692).
2. `lwip/src/api/netbuf.c:110`: `netbuf_alloc()` -> `pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM)`.
3. `lwip/src/core/pbuf.c:274-284`: for `PBUF_RAM`, `alloc_len = ALIGN(sizeof(struct pbuf)) + ALIGN(offset) + ALIGN(length)`
   and `mem_malloc(alloc_len)` = **one contiguous block**.
4. `lwip/src/core/mem.c:207` `mem_malloc()`: with `MEM_LIBC_MALLOC = 1` (`lwipopts.h:98`) it is `malloc()`
   on the ESP-IDF **internal heap** (the same heap as `heap_free_min`). `MEMP_MEM_MALLOC = 1` (`lwipopts.h:105`),
   so lwIP pools are also heap-backed; there is no fixed lwIP pool that could run out separately.
5. `lwip/src/api/err.c:50`: `ERR_MEM` maps to `ENOMEM`. This is the errno the bench reports.

A second path gives the same errno `[sumber]`:
`components/esp_netif/lwip/netif/wlanif.c` `low_level_output()`: if the driver returns `ESP_ERR_NO_MEM`
it is translated to `ERR_MEM` (wlanif.c:113-115), which `lwip_sendto()` returns as `ENOMEM`; the same function
also returns `ERR_MEM` when it must copy a chained pbuf into a new `PBUF_RAM` and the allocation fails
(wlanif.c:96-101). Whether `esp_wifi_internal_tx` itself returns `ESP_ERR_NO_MEM` when its dynamic TX buffer
cannot be allocated: **tidak terverifikasi** (closed-source Wi-Fi library; `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER=y`,
`DYNAMIC_TX_BUFFER_NUM=4` in `firmware/v4-idf61/sdkconfig`).

Conclusion, from reading the source only (nothing was observed inside the stack on the target): `sendto` returns
`ENOMEM` when the lwIP datagram copy cannot be allocated (path 1-5) or when the Wi-Fi driver path returns
`ESP_ERR_NO_MEM`. **Which of these paths failed on the board is not verified**, and the Wi-Fi driver's own TX
buffer allocation is not verifiable (closed-source library).

### 2.2 Heap budget in ENGINEERING

RAW buffer (before) `[probe]`: 128 x 32 B + 128 B + 2016 B scratch + 32 B header = **6272 B**; one contiguous
`heap_caps_malloc`.

One datagram `[sumber]` + `[hitung]`:

- Wire size = 32 B packet header + 16 B RAW header + 14 B per sample (62 B for N=1, 1448 B for N=100, both
  asserted in `test_raw_engineering.c`).
- A packet usually carries one slot: 40 samples -> 32 + 16 + 560 = **608 B**. The encoder takes every queued
  sample with consecutive sequence and the same epoch, up to 100 (`nn2_raw_buffer_encode`), so if `nn2_net`
  runs late the datagram grows up to 1448 B (80 queued samples -> 1168 B).
- lwIP block = 16 (`struct pbuf`, aligned) + 42 (`PBUF_LINK_HLEN` 14 + IP 20 + UDP 8) + wire size, plus
  heap allocator overhead (not measured). N=100: ~1506 B. 40 samples: ~666 B.
- On top of that the Wi-Fi driver allocates its own TX buffer for the frame (dynamic, up to 4):
  size **tidak terverifikasi**; assume about one more datagram (~1.5 KB at most).

Why `ENOMEM` appears now and then, first reading (**superseded by the Batch 4b bench, section 8**):
the 4a figures (`heap_free_min` 3068 B, largest block 5632 B) suggested that short bursts of low total free heap
starve the lwIP pbuf and the Wi-Fi TX buffer. The Batch 4b bench did not support it: with
`heap_free_min` 6448 B (never below 6.4 KB) `sendto` still failed 110 times with errno 12. Total free heap alone
therefore does not explain the failures, but fragmentation of the largest free block during the capture is **not
excluded** (the largest block was only read while STOPPED: 7680 B). A reading that is better supported by the
documentation, though unproven, is a **count limit on Wi-Fi TX buffers**: `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=4`
(`sdkconfig.defaults`, sized for "about 20 kB/s to one station"), the ESP-IDF Kconfig help for this option says
UDP senders can run out of TX buffers when the upper layer delivers faster than the Wi-Fi layer transmits, and
`esp_wifi_internal_tx` documents `ESP_ERR_NO_MEM` ("out of memory") which `wlanif.c` maps to `ERR_MEM` -> errno 12
`[sumber]`. RAW sends about 200 packets/s (about 120 kB/s), roughly 6 times the 20 kB/s design contract. That this
is the exact failing resource is **not proven on target** (no trace of the Wi-Fi driver's allocation).

What the bench data supports `[bench]`: loss only in ENGINEERING; `lost_in_breaks` (samples missing at sequence
breaks, counted by `nn2_raw_capture.py`) divided by 40 equals the `frame_send_errors` delta in 3 of 3 captures
(22, 38, 110). The division assumes 40-sample packets: the encoder can send 1-96 samples per packet, but the
received packets averaged 472844 / 11821 = 40.0 samples. The match is consistent with every lost packet being one
failed `sendto`; it is not a proof that no packet is lost elsewhere.

### 2.3 Options

| Option | Bytes freed | Risk | Status |
|---|---:|---|---|
| **A** shrink RAW buffer 128 -> 96 samples | 1056 (`[probe]`) | Buffer overflow if the consumer stalls longer than the free space allows (12 ms from empty, about 2 ms at the measured high-water 80); shows as `raw_dropped_samples` > 0 and GAP_BEFORE. Measured high-water 80 (Batch 3) leaves 16 samples of headroom. | **Implemented** |
| A' shrink to 80 samples (two slots exactly) | 1584 | Zero headroom over the only measured high-water; any jitter drops samples. | Not recommended |
| **C1** cap samples per packet at 40 in `nn2_raw_buffer_encode` | 0 steady state; lowers the transient peak by up to ~840 B (1506 -> 666 B) | Changes RAW packet sizes/count (more datagrams, 4-frame budget per round is then hit sooner, CPU/airtime up). Protocol effect: needs an issue/ADR. | Analysis only |
| **C2** fewer Wi-Fi dynamic TX buffers (`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=4`) or other lwIP/Wi-Fi options | up to ~1.5 KB per buffer removed from the worst case (unverified) | Needs `sdkconfig` change (forbidden here); lower TX concurrency can raise `EAGAIN`/`ENOBUFS`. lwIP itself has no UDP send queue and no pool to shrink (`MEM_LIBC_MALLOC=1`, `MEMP_MEM_MALLOC=1`). | Analysis only |
| **C3** retry failed `sendto` | 0 | Forbidden (no new retry); also adds work in the net task. | Not done |
| **D** remove the 2016 B codec `scratch` from the buffer (encode straight from entries) | 2016 | Requires a codec API change (forbidden). | Open decision |
| PSRAM | n/a | Board has PSRAM but the pins conflict, so it must not be used. | Struck |

## 3. E2 - option A

### 3.1 Capacity choice

- Unit of consumption: one slot = 40 samples; at least two slots must be queued without dropping: 80.
- Measured high-water: 80 (Batch 3, quoted by the Owner as "80 of 128"). **Limitation:** this figure is not in the repo
  and was measured before Batch 4a; no newer high-water is known (the Batch 4a bench report does not state one).
  The capacity was chosen from that single figure.
- Chosen: 96 = 80 + 16 (20 % headroom; 12 ms of buffering from empty at 8 kSPS, but only 16 entries, about 2 ms, above the measured high-water 80). Overflow behavior is unchanged: newest sample is
  dropped, `dropped` counts it, the next accepted entry gets `GAP_BEFORE`.
- `NN2_RAW_BUFFER_MIN_SAMPLES` = 80 with a `_Static_assert`, so the capacity cannot silently go below two slots.

### 3.2 Why the index code changed

The old code required a power-of-two capacity (`x & MASK`) and used free-running 32-bit `head`/`tail`. The only
power of two that is >= 80 is 128, so option A is impossible without changing the indexing. A plain `x % 96` on a
free-running counter would break when the counter wraps at 2^32 (2^32 is not a multiple of 96; 149 h at 8 kSPS).
So `head`/`tail` now hold positions in `[0, 2 * CAPACITY)`: depth = `(head - tail) mod 2*CAPACITY`
(0 = empty, CAPACITY = full, no ambiguity), entry index = `position mod CAPACITY`. Memory ordering is unchanged
(`ACQUIRE`/`RELEASE` on `head`/`tail`; producer still the only writer of `head`, reader of `tail`).
The lazy allocation (`nn2_app_raw_mode_set`, only on set-mode ENGINEERING while STOPPED) is untouched.

### 3.3 Sizes

| | Old | New |
|---|---:|---:|
| Capacity (samples) | 128 | 96 |
| Entries | 4096 B | 3072 B |
| Gap flags | 128 B | 96 B |
| Scratch + header | 2048 B | 2048 B |
| `sizeof(nn2_raw_buffer_t)` | 6272 B | **5216 B** (`[probe]`: `_Static_assert` passes with the Xtensa ESP32-S3 compiler) |
| Largest RAW datagram from a full buffer | 1448 B (N=100) | 1392 B (N=96) |

### 3.4 Heap estimate in ENGINEERING `[hitung]` (estimate; measured result in section 8)

| Figure | 4a bench | Estimate with 96 samples | Proposed margin |
|---|---:|---:|---|
| `heap_free_now` (ENGINEERING) | ~11.9 KB | ~12.9 KB (+1056 B) | n/a |
| `heap_free_min` | 3068 B | ~4124 B | >= 4096 B (**28 B above; thin, not a robust pass**) |
| largest block | 5632 B | 5632 B ... ~6688 B (depends on whether the freed bytes join the largest free region) | datagram need (~1.5 KB) + 2048 B = ~3.6 KB: met by the 4a value already |

The `heap_free_min` target is met on paper only by 28 B. The allocator overhead of the 5216 B block, Wi-Fi
state differences and the ordinary run-to-run spread can each exceed that. If the bench shows `heap_free_min`
< 4096 B or `frame_send_errors` > 0, **option A alone is not enough**; the next steps are decisions for the
Owner (no other change was added in this batch):

- go to 80 samples (+528 B more, zero headroom);
- option C1 (40-sample packet cap), needs an ADR;
- option D (drop the scratch from the buffer), needs a codec API change;
- relax the proposal: the bench criterion could be "no `frame_send_errors`" instead of a byte count.

## 4. E3 - host tests (class: host/unit)

Added/updated in `test/host/test_raw_engineering.c`:

- full buffer: `CAPACITY + 7` pushes -> `dropped == 7`, `highwater == CAPACITY`; first packet carries `CAPACITY`
  samples; after the hole the next packet has `GAP_BEFORE` (flags 3), first sequence = the sample after the drop;
  counters unchanged by the read;
- codec N=100 still encodes to 1416 B payload and decodes (the buffer can no longer produce N=100, so it is tested
  directly on `nn2_oc_encode_raw`); N=0 invalid and N=101 too-large unchanged; N=1 packet = 62 B unchanged;
- `wrap_keeps_order_and_depth`: 192 cycles of 60 samples, crossing both position wraps with multi-sample reads;
  sequence, timestamp and flags stay contiguous, `dropped == 0`;
- `two_slots_of_40_fit_without_drop`: 80 samples queued, no drop;
- `metadata_survives_ring_reuse` (existing, 2 x CAPACITY + 5 single-sample cycles) passes unchanged;
- ACQ1 bytes test (`acq1_bytes_unchanged`) unchanged and passing;
- `tests/architecture/test_ram_budget.py` passes against the new constants.

## 5. Verification record

Environment of this run: Windows 11, Python 3.13.5, CMake 4.0.3, Ninja 1.12.1, native C compiler = `zig cc`
(ziglang 0.16.0, installed with pip for this run because no GCC/Clang was present); the CI runner uses its own
compiler. ESP-IDF v6.1 and Xtensa GCC 15.2 were used for the compile-only probe only.

| Check | Base `8723e72` | This change |
|---|---|---|
| `run_host_tests.py` unit run | 143 tests, 0 failures | 145 tests, 0 failures |
| `run_host_tests.py` public-opacity probe (inside `--failure-controls`) | fails in this local environment | same failure; CI is the authority |
| `ctest` | 2/2 passed | 2/2 passed |
| `verify-v4-idf61.py` | PASS | PASS (after manifest hash update) |
| `unittest discover tests/architecture` | 22 failures, 3 errors (local env) | identical set of 27 failing items (diff empty) |
| Xtensa compile of `nn2_core_raw_buffer.c` with `-Wall -Wextra -Werror -pedantic` | not run | OK; `sizeof(nn2_raw_buffer_t) == 5216` |
| ESP-IDF build, flash, bench | NOT RUN | NOT RUN |

The architecture-test failures exist unchanged on the base in this local environment (no native `gcc` on PATH);
they are not caused by this change. The authoritative result is the GitHub Actions run on the pushed commit.

## 6. Risk to ADS / timing

No change to the ISR, the ADS task, scheduling, stacks, timing constants or the codec. The only runtime
difference is inside `nn2_raw_buffer_push` (producer: optical consumer, ENGINEERING only): replace a bit mask with
`advance`/`distance`/`index_of` (a few integer compares and one add/subtract each, no loops). Expected effect on `spi_max`, `service_max`, `lag_max`, `late_max`,
`sched_deferred_max`: none; confirm with bench A (ACQ, 10 Hz, 600 s; ACQ does not even allocate the buffer) and
the `timing` output in the ENGINEERING run.

## 7. How to read the bench numbers

- `frame_send_errors`, `frame_send_enobufs`, `frame_send_eagain`, `frame_send_other`, `frame_last_errno`: from
  `get_netdiag`. ENOMEM is counted in `frame_send_other`; `frame_last_errno` 12 = ENOMEM.
- `lost_in_breaks` (from `nn2_raw_capture.py`) divided by 40 should equal the delta of `frame_send_errors` over the
  capture (one packet = one slot = 40 samples).
- `raw_dropped_samples` > 0 means the 96-sample buffer overflowed (consumer lag), distinct from `sendto` loss.
  `raw_buffer_highwater` must stay clearly below 96; at 96 the buffer was full.
- `heap_free_min` is lowest total free internal heap since boot (never rises until reboot); compare it after boot
  + ENGINEERING session, not after returning to ACQ.
- `heap_free_now` after `set-mode ACQ` should return to about 18.5 KB (no leak).

Heap margins in `config/ram_budget.json` remain `PROPOSED`.

## 8. Bench result and Owner decision (2026-11-06)

Bench by the Owner on 2026-11-06 (Asia/Seoul): ESP32-S3 target on COM11, firmware built from commit
`c6c9e7bfe16599f545bc920abf2abd78fc599d60` (ELF build date `Nov  6 2026 18:02:50`, `boot_id` 168), SoftAP, 10 Hz, one ENGINEERING
capture of 60 s (second round not run). Board hardware revision and the exact `sdkconfig` used were not recorded by the Owner
(`sdkconfig.defaults` of that commit applies). Raw key outputs: `docs/evidence/batch4b-bench-2026-11-06.md`. Owner decision
(option 3) was given in the Owner chat session on 2026-11-06 `[bench]`:

| Item | 4a (128 samples) | 4b (96 samples) |
|---|---:|---:|
| `heap_free_min` in ENGINEERING | 3068 B | **6448 B** (>= 4096 B proposal) |
| `heap_free_now` in ENGINEERING / after `set-mode ACQ` | ~11.9 KB / ~18.5 KB | 13196 B / 18584 B (no leak) |
| `frame_send_errors` (all errno 12, `frame_send_other`) | 22 and 38 | 110 |
| `lost_in_breaks` | 880 and 1520 | 4400 (= 110 x 40) |
| Lost fraction | about 0.2-0.3 % | about 0.92 % (110 of 11931 packets) |
| `raw_dropped_samples` / `raw_buffer_highwater` | 0 / n.a. | 0 / 80 (capacity 96) |

ACQ regression (600 s): all error counters 0, `spi_max` 59, `service_max` 73, `lag_max` 31, `late_max` 2,
`sched_deferred_max` 254, `heap_free_min` 10076: no regression. Option A achieved its heap goal (margin and no
leak) but did **not** remove the `sendto` failures. Loss is not explained by total free heap.

**Owner decision (option 3):** accept about 1 % RAW packet loss in ENGINEERING mode. RAW and ENGINEERING frames are
diagnostic data, not science data, and the ENGINEERING mode exceeds the 20 kB/s network design contract. No
`sdkconfig`, protocol, retry or codec change is made. Revised bench criterion for ENGINEERING RAW: loss must be
**counted and detectable**, that is `lost_in_breaks / 40` is consistent with `delta(frame_send_errors)` (a consistency check: it assumes 40-sample packets, the received mean was 40.0003 samples per packet, and `nn2_raw_capture.py` does not count loss before or after the received sample range), `raw_dropped_samples == 0`,
`heap_free_min >= 4096 B`, no heap leak, and no ACQ regression. Zero `frame_send_errors` is no longer required.

Open items (not done here): the Wi-Fi TX buffer count is the likely limit but is unproven; if RAW without loss is
ever needed, the candidates remain `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` (about 0.65 KB per extra buffer in use)
or merging two slots per packet (ADR needed). Heap margins in `config/ram_budget.json` remain PROPOSED.
