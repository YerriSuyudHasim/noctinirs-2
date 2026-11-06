# Batch 4a: observation-only diagnostics, RAM budget gate

Branch: `feat/n4a-diagnostics-ram-budget`. Basis: `02034ef` (PR #89, Batch 3).
This batch only observes: no retry, no budget change, no change to timing, priority,
stack, sdkconfig, codec, golden vectors, the ADS task, ISR DRDY, or any diagnostic
buffer (1413 B / 1349 B).

Status: host and architecture checks in this environment. ESP-IDF build, flash and
bench: **NOT RUN**. Owner bench is required before any PR.

## 1. What changed

| File | Function / symbol | Why |
|---|---|---|
| `components/nn2_net/include/nn2_net.h` | `nn2_net_status_t` | Adds `frame_send_errors`, `frame_send_eagain`, `frame_send_enobufs`, `frame_send_other`, `frame_last_errno`. Existing fields keep their meaning. `s_status` grows 28 -> 48 B (+20 B `.bss`, **approved by Owner, 2026-10-31**). No other static RAM added. |
| `components/nn2_net/nn2_net.c` | `nn2_net_poll` (FRAME loop only) | Counts a failed FRAME `sendto` by class and keeps the last FRAME errno. Errno captured right after `sendto`. Reply path untouched. Single writer: the nn2_net task. |
| `components/nn2_core/include/nn2_core_protocol.h`, `src/nn2_core_protocol.c` | `NN2_CMD_GET_NETDIAG` (0x0032) | New read-only registry entry: argument size 0, cache bypass. Append-only. |
| `components/nn2_app/src/nn2_app_comm.c` | `execute`, `nn2_app_comm_process` | Dispatches `get_netdiag` like `get_health`/`get_timing` (session gate exempt, owner gate unchanged). Read kind 3. Fallback `v4netdiag=0 reason=unavailable`. |
| `firmware/v4-idf61/main/v4_nn2_control.inc` | `control_read_diag` (kind 3 only) | `get_netdiag` text: heap keys, `frame_*`, `net_*`. `get_health` and `get_timing` text is identical to `02034ef`. |
| `firmware/v4-idf61/migration-manifest.json` | one hash (`main/v4_nn2_control.inc`) | Hash the verifier requires for the changed file. |
| `tools/nn2ctl.py` | `COMMANDS["netdiag"]`, `parse_diag`, `describe_reply`, `parse_observation` | Accepts `v4netdiag=1`, requires the netdiag keys, decodes `na`, checks `frame_split_ok` and `net_send_ge_frame`. |
| `config/ram_budget.json` | `.dram0.bss` limit, `s_status` limit | `.dram0.bss` total **not set** (null). `s_status` limit 28 -> 48 (approved). File stays PROPOSED. |
| `tools/check_ram_budget.py` | `check` | A null section limit is reported as `UNSET` and not gated. Symbol limits are still gated, and missing or ambiguous symbols still fail closed. |
| `tests/architecture/test_ram_budget.py` | - | Covers the unset `.bss` limit, the approved `s_status` value, and the gate with an explicit test limit. |
| `tests/architecture/test_batch4a_diagnostics.py` | `NetDiagKeys` | Keys appear only in the netdiag block (including `net_*`); largest-block walk gated to IDLE; net status read once. |
| `tests/architecture/test_nn2ctl_diag.py` | netdiag cases | Decode of heap, frame and `net_*`; `na`; split and net-vs-frame checks; malformed values. |
| `tests/architecture/test_opcode_registry.py` | `test_netdiag_is_registered_read_only_udp_only` | Header, registry, nn2ctl, ADR row, dispatch. |
| `tests/architecture/test_diag_worst_case.py` | netdiag tests (pass), health and timing (`expectedFailure`) | Worst-case reply length against the real capacity. |
| `tests/architecture/test_v4_batch1.py` | stubs only | Stubs for the kind 3 branch so the base formatter test compiles. Assertions are the base ones. |
| `docs/adr/0004-host-protocol.md` | registry row 0x0032, amendment paragraph, `file rev 5` | Owner decision, 2026-10-31. Includes `net_*`. |
| `docs/adr/0004-local-implementation.md` | registry row 0032 | Same. |
| `docs/RAM_BUDGET.md`, `docs/V4_IDF61_SINGLE_CLOCK.md` | - | Pointers and `.bss` note. |

## 2. Keys in `get_netdiag` (`python tools/nn2ctl.py netdiag`)

Reply prefix `v4netdiag=1`. Read only in the nn2_net task (the sole writer of the
net counters and the only caller of this path).

| Key | Meaning | Cost (ESP-IDF v6.1 source) |
|---|---|---|
| `heap_free_now` | Free internal 8-bit heap now. | O(registered heaps), no lock: cheap |
| `heap_free_min` | Lowest free internal heap since boot. | O(registered heaps), no lock: cheap |
| `largest_free_block_now` | Largest free internal block. **Only when STOPPED**; `na` while running. | Walks every block under the heap spinlock (`portENTER_CRITICAL_SAFE`): expensive, so STOPPED only. |
| `frame_send_errors` | FRAME `sendto` failures since boot (any class). Equals the three keys below. | - |
| `frame_send_eagain` | Socket send buffer full (EAGAIN/EWOULDBLOCK). Points to the firmware send side. | - |
| `frame_send_enobufs` | ENOBUFS. Points to the firmware send side. | - |
| `frame_send_other` | Any other error, or a short send (reported as EIO). | - |
| `frame_last_errno` | errno of the last FRAME failure (0 = none). Signed. | - |
| `net_received` | Datagrams received on the UDP socket since boot. | - |
| `net_replied` | Replies sent successfully since boot. | - |
| `net_invalid` | Datagrams the app layer rejected since boot. | - |
| `net_send_errors` | Reply and FRAME send failures together (same meaning as before Batch 4a). | - |
| `net_last_errno` | errno of the last net send or receive failure (signed). | - |

Reading the D1 result: compare `frame_send_errors` with lost packets from the RAW capture
(`lost_in_breaks / 40`). Mostly `frame_send_eagain` / `frame_send_enobufs` near that count
means loss on the firmware send side. Near zero means loss in the Wi-Fi path.
`frame_split_ok` must be `true`. `net_send_ge_frame` must be `true`
(`net_send_errors` includes the FRAME failures). The reply failures are
`net_send_errors - frame_send_errors`.

`get_health` and `get_timing` have no Batch 4a keys. Their text is identical to `02034ef`.

## 3. Capacity and worst case

Diagnostic reply capacity: `read_diag` gets `sizeof(diag) - 64u` = **1349 B**
(`components/nn2_app/src/nn2_app_comm.c:440`; buffer `uint8_t diag[NN2_CORE_PAYLOAD_MAX - 28u + 1u]`
= 1413 B at line 434). A text of 1349 B or more returns 0 and the reply falls back.

Worst case with every counter at 10 digits (`tests/architecture/test_diag_worst_case.py`):

| Reply | Worst-case text | Margin to 1349 B | Status |
|---|---:|---:|---|
| `get_netdiag` IDLE | 364 B | +985 B | fits (test passes) |
| `get_netdiag` RUNNING (`na`) | 356 B | +993 B | fits (test passes) |
| `get_health` (same text as `02034ef`) | 1852 B | -503 B | **over the limit already at `02034ef`** (`expectedFailure`) |
| `get_timing` (same text as `02034ef`) | 2077 B | -728 B | **over the limit already at `02034ef`** (`expectedFailure`) |

**Open finding:** health and timing exceed the 1349 B capacity at worst case on the
base commit, before Batch 4a. The 10-digit case is theoretical, but a reply that overflows
falls back to `v4health=0 reason=unavailable` or `v4timing=0 reason=unavailable`.
Batch 4a does not change this. The two tests are `expectedFailure` and will report if the
text ever fits. Changing the buffer is out of scope.

Payload check (`28 + text + mode suffix <= 1440`): the netdiag worst case with the 48-byte
ENGINEERING suffix is 440 B (IDLE) and 432 B (RUNNING).

## 4. RAM

- Static `.bss`: `s_status` 28 -> 48 B (+20 B), approved by Owner, 2026-10-31. No other static RAM added.
- Heap at boot: no change.
- `.dram0.bss` total: **not set** in `config/ram_budget.json`. It is taken from the Owner's
  linker map at the first bench build (`firmware/v4-idf61/build/NoctiNIRSv4_IDF61.map`).
  Until then `check_ram_budget.py` reports it as `UNSET` and does not gate it.
  Older figures conflict (KONTEKS 172 360 B; `BATCH3_RAW_RAM_FIX.md` arithmetic 178 616 B).
  The Owner's first map decides which is right.
- Heap margins (2 048 B boot, 4 096 B `heap_free_min`) remain PROPOSED.
- The netdiag path runs in the nn2_net task: one `nn2_net_status_t` copy (48 B) on that stack.
  Stack size is unchanged; watch `net_hwm_words` in `get_timing`.

## 5. Verification (this environment)

Commands: `run_host_tests.py --build <tmp> --evidence <tmp> --failure-controls`,
`verify-v4-idf61.py`, `python3 -m unittest discover -s tests/architecture -v`,
`ctest --test-dir <tmp>`. Same commands on `02034ef` (worktree) and on the branch.
ESP-IDF build, flash and bench: **NOT RUN**.

| Command | `02034ef` | Branch |
|---|---|---|
| `run_host_tests.py --failure-controls` (product) | exit 0; 143 tests, 0 failures | exit 0; 143 tests, 0 failures |
| `run_host_tests.py` failure-control run | 1 failure (expected) | 1 failure (expected) |
| `verify-v4-idf61.py` | exit 0, PASS | exit 0, PASS |
| `unittest discover tests/architecture` | exit 0; 127 tests OK | exit 0; 163 tests OK, 1 skipped (`.dram0.bss` unset), 2 expected failures |
| `ctest` | exit 0; 2/2 passed | exit 0; 2/2 passed (61 golden vectors) |

## 6. Bench steps (Owner, laptop)

1. `git fetch origin ; git checkout feat/n4a-diagnostics-ram-budget ; git log -1 --oneline ; git status --short`
2. `idf.py -C firmware\v4-idf61 -p COM11 build flash`
3. `idf.py -C firmware\v4-idf61 -p COM11 monitor`
   - No `BOOT_V4_7_NETWORK_INIT FAIL`. `NN2_HEAP` lines present. WiFi `NoctiNIRS` appears.
4. `python tools/check_ram_budget.py firmware\v4-idf61\build\NoctiNIRSv4_IDF61.map`
   - Expected: `.dram0.bss` shows `UNSET`. Write the measured total into `config/ram_budget.json` only after Owner approval.
5. `python tools/nn2ctl.py info ; python tools/nn2ctl.py status`
6. `python tools/nn2ctl.py health`
   - Must be `v4health=1 ...`. If it is `v4health=0 reason=unavailable`, stop and report it.
7. `python tools/nn2ctl.py timing`
   - Must be `v4timing=1 ...`. If it is `v4timing=0 reason=unavailable`, stop and report it.
8. `python tools/nn2ctl.py netdiag`
   - Must be `v4netdiag=1 ...`. If it is `v4netdiag=0 reason=unavailable`, stop and report it.
   - `largest_free_block_now` is a number when STOPPED and `na` while running.
   - `frame_split_ok` and `net_send_ge_frame` must be `true`.
9. A. Normal-mode regression: `python tools/nn2ctl.py run --rate 10 --seconds 600 --led`, then `timing` and `health`.
   Reference: fault=0, edge_miss=0, adc_missed=0, gap=0, crc_bad=0, spi_errors=0, spi_max<=~60 us,
   service_max<=~100 us, lag_max~33, late_max<=2, sched_deferred_max not clearly higher.
10. B. While a session runs (second window): `python tools/nn2ctl.py netdiag` every ~10 s.
    `largest_free_block_now` must be `na`. Note `heap_free_min`. Timing stays within reference A.
11. C. ENGINEERING and RAW: set-mode ENGINEERING while STOPPED;
    `python tools/nn2_raw_capture.py --seconds 60 --rate 10 --output <path>`; note packets, samples, gap, breaks, lost_in_breaks;
    then `python tools/nn2ctl.py netdiag` and note `frame_send_errors`, `frame_send_eagain`, `frame_send_enobufs`,
    `frame_send_other`, `frame_last_errno`, `net_send_errors`, `heap_free_min`; compare `frame_send_errors` with `lost_in_breaks / 40`;
    set-mode ACQ.

Do not create a PR until the Owner writes "bench 4a lolos, buat PR".
