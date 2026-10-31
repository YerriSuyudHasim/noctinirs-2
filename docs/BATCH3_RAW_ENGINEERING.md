# Batch 3: bounded engineering RAW integration

Base: `e42f2bc9` (merge #87), branch `feat/n3-batch3-raw-engineering`.
The initial repository root, branch, HEAD and clean status were checked before
creating the work branch. Contract v0.4.3 SHA-256 was checked against the Owner's
value. This is local source/host evidence, not bench approval or science output.

## Scope and acceptance

Only R1-R6 from the Owner's Batch 3 request are included. The existing Batch 2
codec and all 61 golden vectors remain unchanged. No OPT2 producer,
SESSION_REPORT, set_gate, gate statistics, safety mechanism, DRDY ISR work,
timing constant, task affinity/priority, I2C speed, pin, watchdog, partition or
legacy port 3334 change is included.

- R1: a fixed 256-entry SPSC buffer receives copies from the sole existing
  optical ADS consumer, after finalized sample-clock classification. Full means
  drop newest and count it; the next accepted post-loss record carries a gap
  marker. Encoding never joins noncontiguous samples. Timing evidence is pending.
- R2: ACQ1's existing callback, payload length, bytes, session envelope and
  sequencing remain unchanged in normal mode. ENGINEERING dispatches a new
  variable-length RAW callback. Empty never sends. The existing nn2_net socket
  sends to the full lease-owner endpoint; no new firmware socket is created.
- R3: cached set_mode 0x0102 accepts one byte (0 ACQ, 1 ENGINEERING) only while
  STOPPED. Boot defaults to ACQ. Raw flags bit 0 is always 1. RAW has no wire
  frame_flags/gate_config_id fields; engineering mode diagnostics expose 4/0.
- R4: info, health and timing expose mode and RAW packet/drop/highwater counters.
  Existing diagnostic keys remain. Counters reset at the next start and remain
  available after STOP; successful sendto alone increments packets_sent.
- R5: the bench receiver owns START/heartbeat/STOP on one control endpoint,
  checks envelope CRC/length and RAW structure, and saves original NN2 datagrams
  plus CSV. It never produces a science interpretation.
- R6: host coverage includes byte-identical ACQ1 envelopes, frame versus message
  sequencing, RAW N=1/N=100 and empty, rejection of N=0/N=101, STOP-only mode,
  overflow/gap/highwater/drop counts, signed int24, sample wrap and RAW received
  during a heartbeat. The new suite is included in the zero-tests failure control.

## Changed files and functions

| Files | Functions / purpose |
|---|---|
| `components/nn2_core/include/nn2_core_raw_buffer.h`, `src/nn2_core_raw_buffer.c` | `nn2_raw_buffer_reset`, `push`, `encode`, `tx_result`: bounded copy, SPSC publication, gap splitting, codec call and successful-TX accounting |
| `components/nn2_core/CMakeLists.txt` | Link the RAW buffer into the shared core |
| `components/nn2_core/include/nn2_core_protocol.h`, `src/nn2_core_protocol.c` | `nn2_core_command`: register cached one-byte set_mode |
| `components/nn2_ports/include/nn2_port_optical.h` | Mode in start configuration, engineering RAW read callback, TX result and RAW info callbacks |
| `components/nn2_app/include/nn2_app_comm.h`, `src/nn2_app_comm.c` | `execute`, `nn2_app_comm_process`, `nn2_app_comm_frame`, `nn2_app_comm_frame_tx_result`: stopped-only mode, diagnostics, mode-specific payload length and TX feedback |
| `components/nn2_net/nn2_net.c` | `nn2_net_poll`: report the existing FRAME sendto result; preserve socket/endpoint behavior |
| `firmware/v4-idf61/components/nocti_v4_optical/include/nocti_v4_optical.h`, `nocti_v4_optical.c` | `canonical_classify` reuses its existing raw phase result; `classify_complete_sample`, `classify_ready_timings`, `stage_raw`, `abort_pending` copy to the optional buffer; `nocti_v4_optical_bind_raw` binds only while stopped |
| `firmware/v4-idf61/components/nocti_v4_optical/CMakeLists.txt` | Declare dependency on the pure shared core buffer |
| `firmware/v4-idf61/main/v4_nn2_control.inc` | `control_worker`, `control_read_raw`, `control_raw_tx`, `control_raw_info`, `control_read_diag`, `control_init`: session binding, low-priority encoding, existing UDP transport and diagnostics |
| `firmware/v4-idf61/migration-manifest.json` | Refresh active provenance, preserve historical hashes and inventory newly used shared files |
| `tools/nn2ctl.py` | `main`: set-mode --mode ACQ/ENGINEERING and clear running-session rejection; `Client.request`: retain RAW datagrams arriving during a transaction |
| `tools/nn2_raw_capture.py` | `decode_raw`, `Capture.accept`, `main`: validation, binary/CSV output and same-endpoint lease/heartbeat handling |
| `test/host/test_raw_engineering.c`, `test_main.c`, `nn2_test.h`, `CMakeLists.txt` | Add/run production Core/Application RAW and ACQ1 regression tests |
| `test/communication/test_communication.c` | `optical_binding`: retain policy text and verify the newly required info mode suffix |
| `tools/run_host_tests.py` | Include raw_engineering in the isolated zero-tests control; do not weaken rejection of an empty test run |
| `tests/architecture/test_optical_codec.py` | Preserve ABI/vector checks; allow only the two explicit RAW-buffer codec users instead of Batch 2's no-runtime-binding assertion |
| `tests/architecture/test_v4_batch1.py` | Exercise actual diagnostic formatter with RAW counters and space reserved for mode; preserve previous keys |
| `tests/architecture/test_raw_capture.py` | Receiver signed limits, CRC/length/flag/phase rejection, files/gaps/wrap, foreign session and heartbeat interception |
| `docs/adr/0004-host-protocol.md`, this file | Scoped RAW wire/registry amendment and local handoff |

## Timing and operational limits

No ADS or scheduler source file, ISR, or legacy network file was changed. The
additional optical-consumer work is bounded sample copying and atomic queue
bookkeeping. Existing classification is reused once, without added allocation,
logging or division. Encoding and UDP transmission run in the existing control
network task. The buffer holds 32 ms at 8 kSPS; Wi-Fi starvation may overflow it.
Sample-index gaps expose transport/ADS loss separately from RAW buffer drops.
An encoder failure leaves the queue unconsumed; it is never silently skipped.

Measure normal mode for 600 s at 10 Hz, then ENGINEERING for 60 s at 10 Hz on the
same board/configuration. Compare spi_max, service_max, lag_max, late_max,
sched_deferred_max, existing fault/integrity counters and stack watermarks, plus
RAW drops/highwater and receiver gaps. Host tests cannot establish any of these
on-target timing results. The binary/CSV receiver is a bench tool only.

The existing FRAME sender stops when the session leaves STREAMING; queued tail
samples at STOP are not a final-session delivery guarantee. No SESSION_REPORT
or exact trailing-loss reconciliation is added in this batch.

## Contract decisions retained for Owner review

1. RAW layout 3 has neither frame_flags nor gate_config_id. This implementation
   preserves the codec and carries the engineering marker in raw_flags, with
   ENGINEERING_UNGATED/gate_config_id=0 as mode diagnostics. Adding those wire
   fields requires a separate contract/codec decision; OPT2 remains out of scope.
2. RAW has no frame_sequence. Its ordering uses the existing shared envelope
   message sequence and contiguous first_sample_sequence+i. ACQ1 frame_sequence
   remains separate and tested. Final session metadata / exact trailing-loss
   accounting remains the later SESSION_REPORT decision.

The existing codec returns NN2_OC_INVALID for N=0 and NN2_OC_TOO_LARGE for N=101,
not NN2_OC_INVALID for both as stated in the initial task context. It was retained
unchanged, and tests assert its actual rejection behavior.

Accountability: Owner authorizes bench and push; independent review and hardware /
RTOS specialist review are pending. Rollback is to stop the session and return
to ACQ mode, or revert this local commit after review. No persistent mode or
configuration migration is added. ESP-IDF build, flash, bench/HIL, board revision,
target binary size and target warnings: NOT RUN / not measured here.

Verification results and full command inventory are recorded below after the
final comparison with the isolated base checkout. No push is authorized before
the Owner writes `bench 3 lolos, push`.


## Final local verification (2026-10-31, before local commit)

Code tested on e42f2bc9 plus this Batch 3 working tree. Native tools: Python
3.13.5, GCC 5.1.0 (MinGW), CMake 3.30.2, Ninja 1.12.1, Windows. Set PATH to the
installed compiler/CMake/Ninja, NN2_TEST_CC and NN2_BATCH1_CC to
C:/Users/doeki/Downloads/MinGW/bin/gcc.exe, and PYTHONUTF8=1. This is host/static
verification only. ESP-IDF build, flash and bench: NOT RUN. No target binary size,
warning count, hardware revision or on-target timing claim is made.

- Product host: base 137/0/0; Batch 3 140/0/0 (tests/failures/ignored).
- --failure-controls: PASS. Deliberate assertion fails with exit 1; zero tests
  fail with exit 1. Public opacity controls also passed.
- ctest product: both test executables pass, including all 61 unchanged golden
  codec vectors.
- Architecture: base 114 tests / 4 failures; Batch 3 118 tests / the same 4
  failures, no errors. GCC 5.1 does not support the C17/gnu17 options used by
  test_c11_positive_effective_flags, test_header_only_contract_compiled,
  test_later_c17_override_fails and test_later_gnu11_override_fails.
- verify-v4-idf61: PASS; reference integrity: PASS, all 319 files unchanged.
- Layer detector, Class S gate, pinned sources and reviewed C11 regression:
  PASS. The 17-command run has exactly the same failure exit-code inventory as
  the isolated base. No new failures remain.
- Unmodified CI ADC control invocation fails because cc is not installed under
  that name. Supplemental --cc with the installed GCC passes its reviewed-source
  failure and new-source success controls.
- Both dependency collectors fail on Windows compiler-path parsing, identically
  on base and Batch 3 (WinError 2). This pre-existing tooling is not changed.
- The full communication build/loopback is blocked on both trees by the existing
  GetTickCount64 declaration issue in old MinGW. The independent communication
  policy target builds and passes 2388 checks after updating the required info
  mode suffix assertion. Live UDP loopback is not claimed as passed.

The new Python RAW receiver tests all pass. The actual diagnostic C formatter
fits the existing datagram budget with RAW counters and room reserved for mode.

Full job command inventory follows. These were run from the appropriate repo
root. The baseline ran the same commands with final2 replaced by baseline2 from
an isolated detached e42f2bc9 checkout. Logs and exact command arguments are in
C:/Users/doeki/AppData/Local/Temp/nn2-batch3/{final2,baseline2}/commands.json;
comparison.json records the equal failure set. PASS does not mean the complete
CI job is green on this local Windows toolchain.

1. Exit 0:

```text
python tools/fetch_regression_sources.py adc
```

2. Exit 1:

```text
python tools/verify_ads131_control_regression.py --evidence C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\control-regression
```

3. Exit 0:

```text
python tools/run_host_tests.py --build C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\host --failure-controls
```

4. Exit 1:

```text
python tools/collect_inner_build.py --build C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\product-dependencies --mode host
```

5. Exit 1:

```text
python tools/collect_adc_adapter_build.py --build C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\build --evidence C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\adapter-dependencies
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
ctest --test-dir C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\build --output-on-failure
```

13. Exit 0:

```text
cmake -S test/communication -B C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\communication -G Ninja
```

14. Exit 1:

```text
cmake --build C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\communication
```

15. Exit 8:

```text
ctest --test-dir C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\communication --output-on-failure
```

16. Exit 1:

```text
python test/communication/test_host.py --loopback C:\Users\doeki\AppData\Local\Temp\nn2-batch3\final2\communication\nn2_loopback.exe
```

17. Exit 0:

```text
pwsh -NoProfile -File tools/verify-reference.ps1
```

Supplemental native commands (PASS for ADC controls and policy target):

```text
python tools/verify_ads131_control_regression.py --cc C:/Users/doeki/Downloads/MinGW/bin/gcc.exe --evidence C:/Users/doeki/AppData/Local/Temp/nn2-batch3/adc-control-native
cmake -S test/communication -B C:/Users/doeki/AppData/Local/Temp/nn2-batch3/comm-native -G Ninja -DCMAKE_C_FLAGS=-D_WIN32_WINNT=0x0600
cmake --build C:/Users/doeki/AppData/Local/Temp/nn2-batch3/comm-native --target nn2_comm_tests
C:/Users/doeki/AppData/Local/Temp/nn2-batch3/comm-native/nn2_comm_tests.exe
```

The full comm-native build was also attempted and remains blocked by the same
GetTickCount64 issue; the OS-version define did not resolve the old header.

## Owner bench steps (verbatim)

```text
  idf.py -C firmware\v4-idf61 build
  idf.py -C firmware\v4-idf61 -p COM11 flash monitor
  python tools/nn2ctl.py info
  A) Regresi mode normal: python tools/nn2ctl.py run --rate 10 --seconds 600 --led ; lalu timing dan health. Acuan harus tetap: fault=0, edge_miss=0, adc_missed=0, gap=0, crc_bad=0, spi_errors=0, spi_max<=~60 us, service_max<=~100 us, lag_max~33, late_max<=2, sched_deferred_max tidak naik berarti.
  B) ENGINEERING: set_mode ENGINEERING saat STOP, lalu tools/nn2_raw_capture.py selama 60 s di 10 Hz; cek raw_dropped_samples dan gap; ulangi timing/health (angka harus tetap dalam acuan A).
  C) Uji tolak: set_mode saat jalan harus ditolak.
```

Final byte check: changed text files were normalized to LF per repository rules;
active migration hashes were recomputed and verify-v4-idf61 passed again.
Historical hashes and codec bytes remain unchanged.
