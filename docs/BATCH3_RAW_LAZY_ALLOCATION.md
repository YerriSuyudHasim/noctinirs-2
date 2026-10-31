# Batch 3: STOP-only RAW allocation and boot heap evidence

Parent `61a7d2e`, branch `feat/n3-batch3-raw-engineering`. Work began from the
expected clean parent. Scope is only the Owner's lazy RAW lifetime, three boot
heap diagnostics, memory audit and related verification. No push/merge or
device operation is performed. Owner explicitly authorized a complete ESP-IDF
build in temporary source/build/config paths; flash and bench remain NOT RUN.

## Resource lifetime

The V4 adapter now keeps only `static nn2_raw_buffer_t *s_raw`, initially NULL.
Boot in default ACQ mode does not allocate a RAW instance. The optional optical
`set_mode` port is called by the single application owner task only after the
existing STOPPED check, and only for an actual mode change:

- ENGINEERING allocates exactly one instance with
  `heap_caps_malloc(sizeof(nn2_raw_buffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`.
  It initializes and binds the candidate, then publishes the pointer and accepts
  the mode. Allocation failure does not bind/reset hardware or start a session;
  the mode stays ACQ. It returns the existing PRECONDITION_FAILED status for
  set_mode, interpreted by nn2ctl as **RAW buffer: not enough internal memory**.
  The reply envelope/prefix and codec are unchanged; no error-text wire extension.
- ACQ first calls the stopped-only optical binding with NULL, then clears the
  owner pointer, then calls heap_caps_free. A failed detach never frees a live
  buffer and retains the old mode. A failed initial bind frees the unpublished
  candidate; the actual optical adapter leaves its binding unchanged on failure.
- Same-mode commands and cached retries do not allocate/free again. START only
  resets the existing instance; STOP preserves it for diagnostics and reuse.
  Only a later STOPPED set_mode ACQ frees it. No data/ISR path allocates or frees.

`nn2_app_raw_mode_set` contains the production lifetime policy behind allocation,
release and binding ports. The V4 binding has no extra persistent manager object;
its port descriptor is automatic. Host tests exercise that same policy, including
release ordering and failure rollback. Optical bind already supports NULL and
checks running/stopping/prepared state before changing its pointer. The existing
ADS stop/drain acknowledgement precedes exposing IDLE; the mode hook also checks
the adapter's IDLE state. Thus allocation/release cannot race an active worker,
RAW consumer or application sender. Readers, counters and TX feedback explicitly
handle a NULL pointer.

Capacity stays **128 samples**, retaining the existing **6272-byte per-instance
guard**. This is 16 ms at 8 kSPS; moving to 256 would require a larger heap block
in the already thin margin. Overflow remains counted drop-newest/GAP_BEFORE.
Full timestamps, epoch, signed ADC values and tags are preserved. RAW counters
remain available after STOP in ENGINEERING; after release in ACQ their existing
keys show zero. Mode default is ACQ after each boot, with no persistent setting.

## Boot diagnostics

`control_heap_point`, called only by control_init, prints one line at each
applicable point, all with INTERNAL|8BIT capabilities and requested stack size:

```text
NN2_HEAP point=before_wifi_init free=... largest=... stack=16384
NN2_HEAP point=after_wifi_start free=... largest=... stack=16384
NN2_HEAP point=net_task_create_failed free=... largest=... stack=16384
```

The failure line is printed after the task API reports failure and before
control_off/nn2_wifi_stop. It observes the heap after any failed-task rollback,
not the allocator's internal instant of failure. Normal boot has the first two
lines; the third is failure-only. There is no repeating diagnostic/data-path
print. Existing health keys are retained: free_heap_before_wifi is the first
snapshot; free_heap_after_wifi and largest_free_block are the second snapshot.
They remain boot snapshots, not current live heap measurements. No secondary
Wi-Fi callback-registration message is treated as the cause of the allocation
failure, and that Wi-Fi code is not changed.

## Complete temporary ESP-IDF build and RAM comparison

The complete firmware build **RUN / PASS** in:

```text
source:    C:/Users/doeki/AppData/Local/Temp/nn2lr/src/firmware/v4-idf61
build:     C:/Users/doeki/AppData/Local/Temp/nn2lr/b
SDKCONFIG: C:/Users/doeki/AppData/Local/Temp/nn2lr/config/sdkconfig
```

Active sources were copied to the temporary source tree; historical reference
files were not copied into the build. Owner sdkconfig was copied before build.
All CONFIG_* values match afterward (zero differences), and the Owner map, ELF
and sdkconfig hashes remain unchanged. Compiled production sources/headers in
the temporary snapshot were checked against the working tree (107 files).

ESP-IDF v6.1 `fff9895c82d744c7237be8847347bdd1b07c6643`, Xtensa GCC 15.2.0,
ESP32-S3, Python 3.13.5, CMake 3.30.2, Ninja 1.12.1. No dependency/tool upgrade or
installation was performed. The installed SDK's activation layout was nonstandard;
its Python requirements/constraints check passed with IDF_TOOLS_PATH pointing to
the installed constraints directory, and existing tool binaries were put on PATH.
No dependency or compiler check was disabled. Firmware compiler warnings: **0**;
SDK Kconfig deprecation notes are retained in the log. Hardware board revision
and runtime behavior are not verified by this build.

| Bytes | 3abe610 Owner evidence | 61a7d2e Owner map | New temporary build |
|---|---:|---:|---:|
| `.dram0.data` | 25192 | 25192 | **25192** |
| `.dram0.bss` | 184632 | 178616 | **172360** |
| `.data + .bss` | 209824 | 203808 | **197552** |
| main `s_raw` symbol | 12288 | 6272 | **4 (pointer)** |
| application `.bin` file | not retained | 908688 | **909440** |

The 3abe610 values are the previously recorded Owner map evidence in
BATCH3_RAW_RAM_FIX.md; that map has since been overwritten. The 61a7d2e map is
the current Owner file dated 2026-10-31 16:06, read only. New .bss decreases by
**6256 B versus 61a7d2e** and **12272 B versus 3abe610**. Replacing a 6272-byte
symbol by a 4-byte pointer saves 6268 symbol bytes; the linked .bss gain is 12 B
smaller because of alignment/padding. There is no permanent full RAW instance.
No new .data is introduced. The instance's 6272 B moves to conditional heap use
only when ENGINEERING is accepted. The larger application binary costs flash,
not an extra static RAW buffer.

The size tool reports 909329 B of image contents; the physical padded .bin is
909440 B. Bootloader .bin is 21168 B; partition table .bin is 3072 B. Link and
partition-size checks passed. This build does not measure free heap, fragmentation
or successful task creation on the device.

### Audit of other Batch 3 static RAM (reported, not reduced)

| Storage | Before Batch 3 / e42f2bc9 | New | Finding |
|---|---:|---:|---|
| `nn2_app_comm_storage_t` / main s_nn2_app | 4096 | 4096 | No static increase; new optional port callback fits existing opaque storage |
| nn2_net globals | BSS 58 + data 4 (payload bytes) | same | Source declaration/status types unchanged; Batch 3 added TX-result call only |
| main s_health / health struct | 1000 | 1000 | Header unchanged; new heap snapshots use existing fields |
| optical s_runtime | 88832 | 88840 | Batch 3 RAW binding pointer adds 8 B including alignment |
| main s_raw_enabled | absent | 1 | Existing Batch 3 flag retained |
| new RAW mode policy module | absent | 0 data/BSS | Ports/locals only; no permanent buffer/manager instance |

The old optical source was compiled separately, without linking, using the new
build's identical configuration/include flags; target nm measured its s_runtime
as 88832 B versus 88840 in the linked image. The public communication storage
declaration, health header and net static declarations were compared to e42f2bc9.
The core RAW buffer and codec have no persistent global instance. No unrelated
runtime storage was shrunk. All sizes above distinguish symbol payload from
whole-section alignment; no full e42f2bc9 image size is invented.

## Changed files and functions

| Files | Functions / reason |
|---|---|
| `components/nn2_ports/include/nn2_port_raw_memory.h` | STOP-only allocator/release/bind interface |
| `components/nn2_app/include/nn2_app_raw_mode.h`, `src/nn2_app_raw_mode.c`, app `CMakeLists.txt` | `nn2_app_raw_mode_set`: reserve once, rollback candidate failure, detach-before-release, no static manager |
| `components/nn2_ports/include/nn2_port_optical.h` | Optional set_mode resource hook |
| `components/nn2_app/src/nn2_app_comm.c` | `execute`: call resource hook after STOP check, preserve mode on failure, map allocation failure to existing PRECONDITION status |
| `firmware/v4-idf61/main/v4_nn2_control.inc` | s_raw pointer; `control_set_mode`, raw allocator/release/bind adapters; worker resets only existing instance; read/TX/diagnostics handle NULL; `control_heap_point` and control_init print three boot points |
| `tools/nn2ctl.py` | `describe_reply`, `require_ok`: exact readable RAW allocation error without a new wire field/status |
| `test/host/test_raw_engineering.c` | Production lifetime-policy tests for failed allocation, unchanged ACQ, single reservation, running/stopping rejection, failed detach retention, release ordering, candidate rollback; retain RAW/ACQ1 regressions |
| `tests/architecture/test_raw_capture.py`, `test_v4_batch1.py` | Error presentation and actual pointer-based diagnostic formatter regression |
| `firmware/v4-idf61/migration-manifest.json` | Active/shared hashes and new module inventory; historical hashes unchanged |
| ADR-0004 and Batch 3 reports | Current allocation exception and historical report links |

No stack size/priority/core, timing, I2C, pin, TWDT, partition, DRDY ISR, codec,
legacy port 3334 or sdkconfig source is changed. There is no reduction of another
task's memory. No flash, monitor, physical SoftAP test or energized bench was run.

## Remaining on-target check

Boot ACQ should recover the permanent RAW reservation, but actual Wi-Fi/task
success still requires Owner bench evidence. A later ENGINEERING reservation
can fail if the remaining largest internal block cannot serve 6272 B plus
allocator overhead; that failure now rejects only the mode change, leaves ACQ
and the control service intact, and reports the explicit error. Do not mistake
compile/link success for heap margin. Preserve every NN2_HEAP line and compare
cold boots; retain the existing 10 Hz normal and engineering timing checks.

Owner retains bench/push authority; independent/hardware review is pending.
Rollback requires stopping the session before reverting firmware; reverting to
61a7d2e restores the known permanent RAW allocation and boot regression risk.
No production release or merge is authorized. Final host results and complete
commands follow below.


## Final verification (2026-10-31, before local commit)

Tests ran on the fixed working tree atop 61a7d2e; the firmware source snapshot
matches that tree. Host/static baseline is the existing clean isolated e42f2bc9
checkout, without creating or switching a worktree/branch in Owner's repo.
Host tools are Python 3.13.5, MinGW GCC 5.1.0, CMake 3.30.2 and Ninja 1.12.1;
NN2_TEST_CC / NN2_BATCH1_CC point to the installed native GCC and PYTHONUTF8=1.

- Product host: **143 tests / 0 failures / 0 ignored**; base 137/0/0; parent 141/0/0.
  New tests verify allocation failure leaves ACQ, same-mode/cached retry reserves
  once, ACQ releases after unbind, failed detach keeps the live object, failed
  initial bind rolls back, and running/stopping mode changes cannot allocate/free.
  Existing N=1/N=100, empty/N=0/N=101 rejection, GAP_BEFORE, sent/drop/highwater,
  sample/message sequence, metadata reuse and byte-identical ACQ1 tests all pass.
- **--failure-controls PASS**: the assertion control and zero-test control both
  fail with exit 1 as required; normal product process exit is 0.
- **ctest 2/2 PASS**, including all **61 unchanged golden codec vectors**.
- Architecture: 119 tests / 4 failures / 0 errors; base 114 tests / the identical
  4 old GCC 5.1 C17/gnu17 incompatibilities / 0 errors. No new failure.
- verify-v4-idf61, Class S/layer detection, pinned source/C11 regression, and
  reference integrity (319 files): PASS.
- All 17 full-job command exit codes match the fresh base run. The local Windows
  job is not called fully green: default cc is missing, dependency collectors
  still misparse native compiler paths, and old MinGW's GetTickCount64 declaration
  blocks the full loopback target/run. These are unchanged baseline failures.
- Supplemental native-GCC ADC controls pass, including required old-fail/new-pass
  and zero-test behavior. The independent control policy target passes
  **2388 checks / 0 failures**. CLI allocation-error presentation and the actual
  pointer-based diagnostic C formatter pass.
- **ESP-IDF complete build and size RUN / PASS**, exit 0, in the temporary paths
  above. Owner map/ELF/sdkconfig remain byte-identical. Generated CONFIG_* macros
  match Owner's existing generated header exactly, as do the sdkconfig values.
- **Flash, monitor, AP visibility, real heap numbers, and bench: NOT RUN.**

Local logs and comparison.json are under
`C:/Users/doeki/AppData/Local/Temp/nn2lr/`. Full host job commands follow; baseline
uses the same arguments with host-final replaced by host-baseline and its own
isolated source root. No generated build artifact or log is committed.

1. Exit 0:

```text
python tools/fetch_regression_sources.py adc
```

2. Exit 1:

```text
python tools/verify_ads131_control_regression.py --evidence C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\control-regression
```

3. Exit 0:

```text
python tools/run_host_tests.py --build C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\host --failure-controls
```

4. Exit 1:

```text
python tools/collect_inner_build.py --build C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\product-dependencies --mode host
```

5. Exit 1:

```text
python tools/collect_adc_adapter_build.py --build C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\adapter-dependencies
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
ctest --test-dir C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\build --output-on-failure
```

13. Exit 0:

```text
cmake -S test/communication -B C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\communication -G Ninja
```

14. Exit 1:

```text
cmake --build C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\communication
```

15. Exit 8:

```text
ctest --test-dir C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\communication --output-on-failure
```

16. Exit 1:

```text
python test/communication/test_host.py --loopback C:\Users\doeki\AppData\Local\Temp\nn2lr\host-final\communication\nn2_loopback.exe
```

17. Exit 0:

```text
pwsh -NoProfile -File tools/verify-reference.ps1
```

Supplemental native commands:

```text
python tools/verify_ads131_control_regression.py --cc C:/Users/doeki/Downloads/MinGW/bin/gcc.exe --evidence C:/Users/doeki/AppData/Local/Temp/nn2lr/adc-control-native
cmake --build C:/Users/doeki/AppData/Local/Temp/nn2lr/host-final/communication --target nn2_comm_tests
C:/Users/doeki/AppData/Local/Temp/nn2lr/host-final/communication/nn2_comm_tests.exe
C:/Users/doeki/AppData/Local/Temp/nn2lr/host-final/build/nn2_optical_codec_tests.exe
```

Complete ESP-IDF commands (the installed venv Python invokes idf.py; environment
uses IDF_PATH=C:/esp/v6.1/esp-idf, IDF_PYTHON_ENV_PATH=C:/Espressif/tools/python/v6.1/venv,
IDF_TOOLS_PATH=C:/Espressif/tools, ESP_IDF_VERSION=6.1, and installed tools on PATH):

```text
C:/Espressif/tools/python/v6.1/venv/Scripts/python.exe C:/esp/v6.1/esp-idf/tools/idf.py -C C:/Users/doeki/AppData/Local/Temp/nn2lr/src/firmware/v4-idf61 -B C:/Users/doeki/AppData/Local/Temp/nn2lr/b -DSDKCONFIG=C:/Users/doeki/AppData/Local/Temp/nn2lr/config/sdkconfig build
C:/Espressif/tools/python/v6.1/venv/Scripts/python.exe C:/esp/v6.1/esp-idf/tools/idf.py -C C:/Users/doeki/AppData/Local/Temp/nn2lr/src/firmware/v4-idf61 -B C:/Users/doeki/AppData/Local/Temp/nn2lr/b -DSDKCONFIG=C:/Users/doeki/AppData/Local/Temp/nn2lr/config/sdkconfig size
```

## Owner bench (one build/flash sequence)

Run the following commands from the repo. WiFi NoctiNIRS must appear; boot log
must not contain BOOT_V4_7_NETWORK_INIT FAIL. Copy **every NN2_HEAP line**, including
the failure-only third point if present. No such on-target numbers are fabricated
in this report.

```text
idf.py -C firmware\v4-idf61 -p COM11 build flash
idf.py -C firmware\v4-idf61 -p COM11 monitor
python tools/nn2ctl.py info
```

A) Regresi mode normal: `python tools/nn2ctl.py run --rate 10 --seconds 600 --led`;
lalu timing dan health. Acuan harus tetap: fault=0, edge_miss=0, adc_missed=0,
gap=0, crc_bad=0, spi_errors=0, spi_max<=~60 us, service_max<=~100 us, lag_max~33,
late_max<=2, sched_deferred_max tidak naik berarti.

B) ENGINEERING saat STOP:

```text
python tools/nn2ctl.py set-mode --mode ENGINEERING
python tools/nn2_raw_capture.py --seconds 60 --rate 10
python tools/nn2ctl.py timing
python tools/nn2ctl.py health
python tools/nn2ctl.py set-mode --mode ACQ
```

Cek raw_dropped_samples dan gap; angka timing/health harus tetap dalam acuan A.
Bila alokasi ditolak, salin pesan RAW buffer: not enough internal memory dan
pastikan info masih mode=ACQ; jangan menganggap capture lulus.

C) Uji tolak: set_mode saat jalan harus ditolak. Ulangi siklus STOP-mode untuk
memeriksa reserve/release; tidak ada malloc/free yang diizinkan saat sesi berjalan.
