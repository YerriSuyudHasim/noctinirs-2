# RAM budget (internal DRAM and heap), V4 / ESP32-S3

Status: **PROPOSAL for Owner review.** Limits live in `config/ram_budget.json`.
Check with `tools/check_ram_budget.py` after `idf.py build`. Nothing here is a
production or science claim. ESP-IDF build, flash and on-target measurement for
Batch 4a: **NOT RUN** in this environment.

Source tags: `[bench]` = Owner bench report (KONTEKS, Batch 3). `[map]` = Owner
linker map recorded in `docs/BATCH3_RAW_RAM_FIX.md`. `[hitung]` = recomputed here.
`[sumber]` = read from source. `[probe]` = compile-only size probe (Xtensa GCC 15.2,
recorded in `docs/BATCH3_RAW_RAM_FIX.md`).

## 1. Static RAM (linker map, `.dram0.data` / `.dram0.bss`)

| Item | Bytes | Tag | Notes |
|---|---:|---|---|
| `.dram0.data` total | 25 192 | `[bench]` `[map]` | Limit in config |
| `.dram0.bss` total | 172 360 | `[bench]` | Limit in config. **Unresolved:** `[map]` 3abe610 showed 184 632; minus 6 016 (RAW fix) gives 178 616 `[hitung]`. Owner must confirm which total the current build has. |
| `nocti_v4_optical` `s_runtime` | 88 840 | `[bench]` | Optical runtime |
| `nocti_v4_scheduler` `s_runtime` | 36 152 | `[bench]` | Scheduler |
| `nocti_v4_ads` `s_runtime` | 18 360 | `[bench]` | ADS |
| `nocti_v4_async_led` `s_runtime` | 8 104 | `[map]` | Async LED |
| `main` `s_nn2_app` | 4 096 | `[bench]` | Communication storage |
| `nn2_net` `s_status` | 28 -> **48** | `[hitung]` `[sumber]` | Batch 4a adds 20 B (frame counters). **Needs Owner approval.** |
| `main` `s_raw` (pointer only) | 4 | `[sumber]` | RAW buffer is no longer static (see section 2) |
| Other `.bss` (not registered) | 16 776 | `[hitung]` | Remainder of `.bss` after the registered items above |

Limits in `config/ram_budget.json`: symbol limits are the bench figures above, and `nn2_net.s_status`
is 48 B (Owner approval, 2026-10-31). The `.dram0.bss` total is **not set**. It is taken from the
Owner's linker map at the first bench build, and `check_ram_budget.py` reports it as `UNSET`.

## 2. Dynamic RAW buffer (heap, only while ENGINEERING and STOPPED)

| Item | Bytes | Tag |
|---|---:|---|
| `nn2_raw_buffer_t` (128 x 32 B entries + 128 B gap bytes + 2 016 B codec scratch + header) | <= 6 272 | `[probe]` `[sumber]` (`_Static_assert` in `nn2_core_raw_buffer.h`) |

Allocated by `set-mode ENGINEERING` in STOPPED state; freed on set-mode back to ACQ.
Not present in ACQ mode. Heap cost is therefore the total in section 3 minus 6 272
while ENGINEERING is active.

## 3. Heap at boot and the network task

Network task: stack 16 384 B (`NOCTI_V4_RTOS_STACK_NET_BYTES`) + static TCB 340 B
(`StaticTask_t`, `[probe]`) = **16 724 B** requirement (allocator overhead not included).

| Snapshot | Bytes | Tag |
|---|---:|---|
| `free_heap_before_wifi` | 79 644 | `[bench]` |
| `free_heap_after_wifi` | 28 352 | `[bench]` |
| `largest_free_block` (after Wi-Fi start, before net task) | 19 456 | `[bench]` |
| Margin of largest block over requirement | **2 732** | `[hitung]` |

Those three boot values are unchanged by Batch 4a (serial `NN2_HEAP` line and
`free_heap_*` / `largest_free_block` keys are the same code path).

Live values from `get_netdiag` (Batch 4a, see `BATCH4A_DIAGNOSTICS.md`): `heap_free_now`,
`heap_free_min` (`heap_caps_get_free_size` / `heap_caps_get_minimum_free_size`, both O(1) per
ESP-IDF v6.1 source), `largest_free_block_now` (expensive, STOPPED only, else `na`).

## 4. Rules

1. No new static RAM (`.data`/`.bss`) and no new allocation at boot that reduces
   a margin in sections 1 to 3 without Owner approval, recorded in the commit message.
2. A limit in `config/ram_budget.json` changes only with that Owner approval.
3. Dynamic allocation on the data path is not allowed (existing project rule).
4. The RAW buffer stays within 6 272 B (`_Static_assert`). The gate and
   `tests/architecture/test_ram_budget.py` must pass before any bench run.

## 5. Proposed margins (USULAN, not decided)

| Margin | Proposal | Current | Reason |
|---|---:|---:|---|
| Boot largest free block minus 16 724 B | >= 2 048 B | 2 732 | About 1/8 of the net stack. Covers allocator overhead and fragmentation at boot. |
| Lowest free heap since boot (`heap_free_min`) in normal and ENGINEERING sessions | >= 4 096 B | not yet measured | Leaves room for the 6 272 B RAW allocation plus Wi-Fi/lwIP runtime allocations. A starting value to revise after bench data. |

Basis: the Batch 3 bench snapshot and the probe sizes only. No repeated cold-boot
or long-session data exists yet, so these are starting points, not limits from measurement.
