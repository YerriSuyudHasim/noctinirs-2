# Batch 3 RAW static RAM follow-up

Parent: `3abe610`, branch `feat/n3-batch3-raw-engineering`. The work branch and
clean parent were checked before editing. Scope is the Owner-reported SoftAP
boot regression and bounded RAW RAM reduction only. No push, firmware build,
flash or energized bench operation is performed by this follow-up.

## Evidence and causal boundary

Owner reports that Wi-Fi entered SoftAP, then stopped after
`BOOT_V4_7_NETWORK_INIT FAIL result=ESP_ERR_NO_MEM`. In `control_init`, the
reported failure path is creation of `v4_nn2_net`, followed by `control_off` and
`nn2_wifi_stop`. The 16384-byte network task stack is unchanged.

The retained Owner build map is
`firmware/v4-idf61/build/NoctiNIRSv4_IDF61.map`, dated 2026-10-31 15:23 and
attributed by Owner to 3abe610. It was read only. Other projects' maps were not
used as baselines. Owner confirmed that the e42f2bc9 V4 map was overwritten and
is unavailable, and authorized the comparison using s_raw's added static cost.

| Measurement | e42f2bc9 | Owner build 3abe610 | Follow-up |
|---|---:|---:|---:|
| `.dram0.data` | unavailable | 25192 B (`0x6268`) | NOT BUILT |
| `.dram0.bss` | unavailable | 184632 B (`0x2d138`) | NOT BUILT |
| `.data + .bss` | unavailable | 209824 B | NOT BUILT |
| static `s_raw` | absent (source verified) | 12288 B (`0x3000`) | 6272 B (`0x1880`), target type-size probe |
| RAW entries | none | 256 x 40 B | 128 x 32 B |
| encoder scratch | none | 2016 B | 2016 B |

The addition of s_raw alone consumes **12288 B more static internal DRAM** than
the base; it is not a heap allocation. It remains resident even in default ACQ
mode. Other Batch 3 changes can have their own small alignment/code effects, so
this is an exact component delta, not a fabricated full-image baseline size.
The follow-up reduces that component by **6016 B**, without taking memory from
any other task or subsystem. The final linked image totals remain NOT RUN.

Largest retained DRAM symbols in the Owner map:

| Symbol / owning component | Bytes |
|---|---:|
| `s_runtime`, nocti_v4_optical | 88840 |
| `s_runtime`, nocti_v4_scheduler | 36152 |
| `s_runtime`, nocti_v4_ads | 18360 |
| `s_raw`, main | 12288 |
| `s_runtime`, nocti_v4_async_led | 8104 |
| `s_nn2_app`, main | 4096 |

None of the other runtime allocations above is reduced. Map DRAM segment origin
is 0x3fc88000, length 0x53700, and `_heap_low_start` is 0x3fcdb4a0. The difference
to that linker segment's end is only 608 B. **That is static linker slack, not
the run-time free heap or its largest block.** ESP-IDF registers additional
eligible regions; Wi-Fi/task allocations and fragmentation are not in the map.
Neither the map's `_heap_end=0x40000000` nor its segment slack can establish the
largest internal block at the failing call.

Using the Owner build's compiler (Xtensa GCC 15.2.0), generated configuration
and include flags, a separate compile-only type probe outside the Owner build
directory measured:

- `sizeof(nn2_raw_entry_t)`: 40 -> 32 B.
- `sizeof(nn2_raw_buffer_t)`: 12288 -> 6272 B.
- `sizeof(nn2_oc_raw_t)`: 2016 B unchanged.
- `sizeof(StackType_t)`: 1 B; network stack request: 16384 B unchanged.
- `sizeof(StaticTask_t)`: 340 B (the SDK's TCB size-equivalence type).

ESP-IDF 6.1 `heap_idf.c` uses INTERNAL|8BIT heap for FreeRTOS allocation. Its
task creation path requires a stack allocation and a TCB allocation. Therefore
this task needs at least **16384 + 340 = 16724 B of allocation payload**, plus
allocator/alignment overhead, and a block that can serve the 16384-byte stack
request. Total free bytes alone do not establish that such a block exists.
The exact failing allocation (stack or TCB) and pre-call largest block were not
recorded in 3abe610. Static RAM pressure is proven; those dynamic measurements
are not claimed. No full ESP-IDF build was run for the probe.

Owner artifacts retained unchanged, SHA-256:

```text
map: e0d516ef88f132cef0993534045a9ade29310c3a98d5765c8f685b7c60b60c7c
elf: e6755ba2fa6f5dc9083c224fadb85318f056e0dd4c1d373a54eda8c7372f685d
```

## RAM design and functional preservation

1. Compact entries first. Store the measured u64 timestamp, four signed i32
   ADC values, u32 sequence, u16 epoch and phase/slot bytes directly in a
   32-byte entry. Keep gap flags in a separate byte array. This avoids the old
   embedded-sample/timestamp/trailing-bool padding. Full timestamps and epochs
   are preserved rather than inferred from a nominal clock or session assumption.
2. Keep codec scratch static and reader-owned. The unchanged typed codec needs
   a 2016-byte input object; its wire output is smaller and cannot safely serve
   as that object. Moving scratch onto a task stack or borrowing another task's
   storage is not used as a RAM workaround. At 256 entries, the compact design
   would still need 10496 B, so entry compaction alone cannot meet this budget.
3. Reduce capacity last, to the permitted minimum of 128. Final size is 6272 B
   (6.125 KiB, including the explicit 128 gap bytes), about 6 KB. Compile-time
   checks enforce this bound, the 32-byte entry bound and minimum capacity 128.
   An isolated negative compile with capacity raised back to 256 fails on the
   RAM budget assertion.

At 8 kSPS the queue retains 16 ms instead of 32 ms. Longer consumer/Wi-Fi delays
can therefore cause more raw_dropped_samples. Overflow remains drop-newest;
older buffered packets are not incorrectly marked, and the next post-loss
packet carries GAP_BEFORE. No allocation, log, division, new consumer or ISR
operation is introduced. N=1..100, CRC/envelope/wire layout, measured timestamps,
phase/slot tags, epoch, sequencing and successful-send accounting are unchanged.

## Changed files and functions

| File | Functions / reason |
|---|---|
| `components/nn2_core/include/nn2_core_raw_buffer.h` | Entry field layout, separate gap bytes, capacity and `_Static_assert` RAM/minimum-capacity guards |
| `components/nn2_core/src/nn2_core_raw_buffer.c` | `nn2_raw_buffer_push`, `nn2_raw_buffer_encode`: copy the compact fields and preserve gap ownership and codec input |
| `firmware/v4-idf61/main/v4_nn2_control.inc` | `control_init`: snapshot existing internal-heap health fields before Wi-Fi and immediately before network-task creation; `control_read_diag`: expose them in health |
| `firmware/v4-idf61/migration-manifest.json` | Refresh hashes of the changed active/shared files; historical hashes retained |
| `test/host/test_raw_engineering.c` | `full_buffer_gap_and_codec_limits`: test the capacity-relative loss marker with old records still queued; `metadata_survives_ring_reuse`: exact irregular u64 timestamps, epochs, phases, signed ADC data and u32 sequence wrap over repeated reuse |
| `tests/architecture/test_v4_batch1.py` | Actual C health formatter includes heap metrics and still fits the existing packet budget with prior keys |
| `docs/adr/0004-host-protocol.md` | Update the current RAW queue capacity/budget description without changing wire format |
| `docs/BATCH3_RAW_ENGINEERING.md`, this file | Mark the original 256-entry report as historical and record the RAM follow-up |

No stack size, priority, core, timing constant, I2C setting, pin, TWDT, partition,
DRDY ISR, codec, reference snapshot or legacy 3334 code is changed. No dynamic
allocation is introduced on the data path. Owner's build files are not built,
overwritten or removed.

## Remaining risk and Owner measurement

NO_MEM can recur if the remaining internal heap is too small or fragmented.
Saving 6016 static bytes improves the budget; it does not prove that the next
boot can satisfy every allocation. Query `health` after boot and compare
`free_heap_before_wifi`, `free_heap_after_wifi` and `largest_free_block` against
the base measured at the same points/configuration. In this control binding,
the latter two are boot snapshots taken after Wi-Fi start and just before the
network task is created, not live post-task heap values. They use existing
health storage. Missing old-image fields are not zero-byte measurements.

Require that the largest internal block can serve the unchanged 16384-byte
stack request, with enough total capacity for the TCB and allocator overhead.
Observe margin over repeat cold boots and after normal/RAW sessions; a single
successful boot does not establish a robust margin. If SoftAP still fails,
health cannot be queried through it: retain the complete serial boot log and
do not claim bench success.

Accountable bench/push authority: Owner. Independent and hardware/RTOS specialist
review remain pending. Rollback is to stop acquisition and review/revert the
follow-up; reverting restores the known 12 KB RAW footprint. This change makes
no production, timing or science claim. Final build size, target warnings,
ESP-IDF firmware build, flash and bench: **NOT RUN**.

Final verification results and commands are appended after the fresh host/static
comparison with e42f2bc9. The existing isolated baseline checkout is used without
creating or switching any worktree/branch in the Owner repo.


## Final verification (2026-10-31, before local commit)

Fresh source/host runs used the fixed working tree on 3abe610 and the existing,
clean, isolated e42f2bc9 checkout. Native tools: Python 3.13.5, MinGW GCC 5.1.0,
CMake 3.30.2 and Ninja 1.12.1. PATH points to those installed tools;
NN2_TEST_CC and NN2_BATCH1_CC point to the installed GCC; PYTHONUTF8=1.
The target type probe used the Owner compiler/config (GCC 15.2.0, ESP32-S3),
with all object/output files outside the Owner build directory. This is not an
ESP-IDF firmware build, link, flash or bench result.

- Product host: **141 tests / 0 failures / 0 ignored**, versus base 137/0/0.
  All RAW and ACQ1 tests pass, including N=1, N=100, rejection N=0/N=101, mode,
  message/sample sequencing, overflow GAP_BEFORE, dropped/sent/highwater counts,
  and exact metadata over ring reuse. The parent had 140 passing host tests.
- **--failure-controls PASS**: an intentional assertion failure returns 1 and
  zero tests return 1. Public opacity controls pass.
- **ctest 2/2 PASS**, including all **61 unchanged golden codec vectors**.
- Architecture: **118 tests, 4 failures, 0 errors**; base 114 tests, the identical
  4 failures and no errors. The four C11 contract failures are the same old GCC
  5.1 C17/gnu17 option incompatibilities as before; no new failures.
- **verify-v4-idf61 PASS**, **Class S/layer gate PASS**, pinned-source and C11
  reviewed-helper regression checks PASS. **Reference integrity PASS**, 319 files.
- All 17 commands below have exactly the same exit-code inventory as the fresh
  base run. The full local job is not called green: unchanged Windows problems
  remain (default `cc` missing; dependency-collector compiler-path parsing;
  old MinGW GetTickCount64 declaration blocking the full loopback build/run).
- Supplemental ADC regression using --cc with the actual GCC passes, including
  the required old-fail/new-pass and zero-test controls. The independent
  communication policy target passes **2388 checks / 0 failures**.
- Negative compile: changing capacity to 256 in an isolated copy while retaining
  the 6272-byte budget fails with `RAW static RAM budget exceeded`.
- Actual C diagnostic formatter fits the existing packet budget with all old
  keys and the added heap snapshots for the ten-minute bench scenario.

Logs, command arguments, target type-size objects and comparison.json are in
`C:/Users/doeki/AppData/Local/Temp/nn2-batch3-ramfix/`. These are local evidence;
no generated binary or log is committed. The full command inventory is:

1. Exit 0:

```text
python tools/fetch_regression_sources.py adc
```

2. Exit 1:

```text
python tools/verify_ads131_control_regression.py --evidence C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\control-regression
```

3. Exit 0:

```text
python tools/run_host_tests.py --build C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\host --failure-controls
```

4. Exit 1:

```text
python tools/collect_inner_build.py --build C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\product-dependencies --mode host
```

5. Exit 1:

```text
python tools/collect_adc_adapter_build.py --build C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\adapter-dependencies
```

6. Exit 0:

```text
python tools/layer_contract.py detect
```

7. Exit 0:

```text
python tools/class-s-gate.py .
```

8. Exit 0:

```text
python tools/fetch_regression_sources.py c11
```

9. Exit 0:

```text
python tools/verify_c11_regression.py
```

10. Exit 1:

```text
python -m unittest discover -s tests/architecture -v
```

11. Exit 0:

```text
python tools/verify-v4-idf61.py
```

12. Exit 0:

```text
ctest --test-dir C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\build --output-on-failure
```

13. Exit 0:

```text
cmake -S test/communication -B C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\communication -G Ninja
```

14. Exit 1:

```text
cmake --build C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\communication
```

15. Exit 8:

```text
ctest --test-dir C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\communication --output-on-failure
```

16. Exit 1:

```text
python test/communication/test_host.py --loopback C:\Users\doeki\AppData\Local\Temp\nn2-batch3-ramfix\final\communication\nn2_loopback.exe
```

17. Exit 0:

```text
pwsh -NoProfile -File tools/verify-reference.ps1
```

Supplemental commands:

```text
python tools/verify_ads131_control_regression.py --cc C:/Users/doeki/Downloads/MinGW/bin/gcc.exe --evidence C:/Users/doeki/AppData/Local/Temp/nn2-batch3-ramfix/adc-control-native
cmake --build C:/Users/doeki/AppData/Local/Temp/nn2-batch3-ramfix/final/communication --target nn2_comm_tests
C:/Users/doeki/AppData/Local/Temp/nn2-batch3-ramfix/final/communication/nn2_comm_tests.exe
C:/Users/doeki/AppData/Local/Temp/nn2-batch3-ramfix/final/build/nn2_optical_codec_tests.exe
```

The target size probe compiles only a separate source containing char arrays
sized by sizeof (raw buffer, entry, scratch, StaticTask_t, StackType_t, and net
stack constant); it reads the Owner compile_commands configuration, removes
all dependency/output flags and directs every probe output to the evidence
folder. Target nm reports the sizes above. The Owner ELF size report is read
with the installed `xtensa-esp32s3-elf-size -A`; neither operation builds or
modifies the Owner firmware.

ESP-IDF firmware build (base or fixed), flash, monitor, SoftAP visibility and
normal/RAW bench: **NOT RUN**. Wi-Fi recovery and heap margin require Owner
verification. Push is not authorized.

## Owner bench steps

First run the single combined build/flash command, then monitor. WiFi NoctiNIRS
must appear and the boot log must not contain BOOT_V4_7_NETWORK_INIT FAIL.
The prior requested steps follow verbatim after these two added commands:

```text
  idf.py -C firmware\v4-idf61 -p COM11 build flash
  idf.py -C firmware\v4-idf61 -p COM11 monitor
  idf.py -C firmware\v4-idf61 build
  idf.py -C firmware\v4-idf61 -p COM11 flash monitor
  python tools/nn2ctl.py info
  A) Regresi mode normal: python tools/nn2ctl.py run --rate 10 --seconds 600 --led ; lalu timing dan health. Acuan harus tetap: fault=0, edge_miss=0, adc_missed=0, gap=0, crc_bad=0, spi_errors=0, spi_max<=~60 us, service_max<=~100 us, lag_max~33, late_max<=2, sched_deferred_max tidak naik berarti.
  B) ENGINEERING: set_mode ENGINEERING saat STOP, lalu tools/nn2_raw_capture.py selama 60 s di 10 Hz; cek raw_dropped_samples dan gap; ulangi timing/health (angka harus tetap dalam acuan A).
  C) Uji tolak: set_mode saat jalan harus ditolak.
```
