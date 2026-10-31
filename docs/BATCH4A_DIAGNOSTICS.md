# Batch 4a: observation-only diagnostics, RAM budget gate

Branch: `feat/n4a-diagnostics-ram-budget`. Base: `02034ef` (PR #89, Batch 3),
checked before editing. This batch only **observes**. It does not change delivery
(no retry, no budget change), timing constants, task stacks, priorities, ADS/ISR
paths, codec, golden vectors, port 3334, `reference/`, or the set-mode command.

Status: host and architecture checks in this environment. ESP-IDF build, flash
and bench: **NOT RUN**. Owner bench is required before any PR.

## 1. What changed

| File | Function / symbol | Why |
|---|---|---|
| `components/nn2_net/include/nn2_net.h` | `nn2_net_status_t` | Adds `frame_send_errors`, `frame_send_eagain`, `frame_send_enobufs`, `frame_send_other`, `frame_last_errno`. Existing fields keep their meaning. Adds 20 B to `s_status` (28 -> 48 B). **Needs Owner approval** (RAM rule). |
| `components/nn2_net/nn2_net.c` | `nn2_net_poll` (FRAME loop only) | Counts a failed FRAME `sendto` by class and keeps the last FRAME errno. Captures `errno` right after `sendto`. Reply path untouched. Single writer: the nn2_net task. |
| `components/nn2_core/include/nn2_core_protocol.h`, `src/nn2_core_protocol.c` | `NN2_CMD_GET_NETDIAG` (0x0032) | New read-only registry entry: argument size 0, cache bypass. Append-only. |
| `components/nn2_app/src/nn2_app_comm.c` | `execute`, `nn2_app_comm_process` | Dispatches `get_netdiag` like `get_health`/`get_timing` (session gate exempt, owner gate unchanged). Read kind 3. Fallback `v4netdiag=0 reason=unavailable`. |
| `firmware/v4-idf61/main/v4_nn2_control.inc` | `control_read_diag` (kind 3) | Adds the netdiag reply: `heap_free_now`, `heap_free_min`, `largest_free_block_now`, `frame_*`. Health and timing text is the pre-Batch-4a text (no change from `02034ef`). |
| `firmware/v4-idf61/migration-manifest.json` | four hashes | Only the hashes the verifier requires for the changed files. |
| `tools/nn2ctl.py` | `COMMANDS["netdiag"]`, `parse_diag`, `describe_reply`, `parse_observation` | Accepts `v4netdiag=1`, requires the netdiag keys, decodes `na`, checks `frame_split_ok`. |
| `config/ram_budget.json`, `tools/check_ram_budget.py` | - | RAM budget gate (PROPOSED). Unchanged in this round. |
| `tests/architecture/test_batch4a_diagnostics.py` | `NetDiagKeys` | Keys appear only in the netdiag block; largest-block walk gated to IDLE; net status read once. |
| `tests/architecture/test_nn2ctl_diag.py` | netdiag cases | Decode, `na`, split mismatch, malformed values. |
| `tests/architecture/test_opcode_registry.py` | `test_netdiag_is_registered_read_only_udp_only` | Header, registry, nn2ctl, ADR row, dispatch. |
| `tests/architecture/test_diag_worst_case.py` | netdiag test (passes), health/timing (expected failure) | Worst-case reply length against the real capacity. |
| `tests/architecture/test_ram_budget.py`, `test_v4_batch1.py` | - | `test_v4_batch1.py` is back at the base version. |
| `docs/adr/0004-host-protocol.md` | registry row 0x0032, amendment paragraph, revision `file rev 5` | Owner decision, 2026-10-31. |
| `docs/adr/0004-local-implementation.md` | registry row 0032 | Same. |
| `docs/RAM_BUDGET.md`, `docs/V4_IDF61_SINGLE_CLOCK.md` | - | Pointers updated to `get_netdiag`. |

## 2. Keys and how to read them

Keys in **`get_netdiag`** (`v4netdiag=1`), read only in the nn2_net task:

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

`get_health` and `get_timing` have no Batch 4a keys. Their content is unchanged from `02034ef`.

Reading the D1 result: compare `frame_send_errors` with lost packets from the RAW capture (`lost_in_breaks / 40`). Mostly `frame_send_eagain`/`frame_send_enobufs` near that count means loss on the firmware send side. Near zero means loss in the Wi-Fi path. `frame_split_ok` in nn2ctl must be `true`.

Not exposed yet: the old `net_*` counters (`received`, `replied`, `invalid`, `send_errors`, `last_errno`). They are not part of the Owner decision. `send_errors` still counts reply and FRAME failures together.

## 3. Why a new opcode, and the measured limit

Diagnostic reply capacity: `read_diag` gets `sizeof(diag) - 64u` = **1349 B** (`components/nn2_app/src/nn2_app_comm.c:440`, buffer `uint8_t diag[NN2_CORE_PAYLOAD_MAX - 28u + 1u]` = 1413 B at line 434). A text of 1349 B or more returns 0, and the reply falls back.

Worst case measured with every counter at 10 digits (`tests/architecture/test_diag_worst_case.py`):

| Reply | Worst-case text | Margin to 1349 B | Status |
|---|---:|---:|---|
| `get_netdiag` IDLE | 240 B | +1109 B | fits (test passes) |
| `get_netdiag` RUNNING | 232 B | +1117 B | fits (test passes) |
| `get_health` (pre-Batch-4a content) | 1852 B | -503 B | **already over** before Batch 4a |
| `get_timing` (pre-Batch-4a content) | 2077 B | -728 B | **already over** before Batch 4a |

Health and timing exceed the limit with the base content. The 10-digit worst case is theoretical, but a reply that overflows falls back to `v4health=0` / `v4timing=0`. Batch 4a does not change that. The two tests are `expectedFailure` and will flag if the content ever fits. Changing the buffer is out of scope.

Payload check (`28 + text + mode suffix <= 1440`): the netdiag worst case with the 48-byte ENGINEERING suffix is 316 B, well under 1440 B.

## 4. Other changes and checks

- No new allocation, log, ISR, ADS or scheduler code. No timing, priority, stack, sdkconfig, codec, golden-vector or buffer size changed.
- Static RAM: +20 B `.bss` (`s_status`). Heap at boot: no change.
- The netdiag path (`control_read_diag` kind 3) runs in the nn2_net task. Its stack use is one `nn2_net_status_t` copy (48 B) plus the reply text on the existing 1413 B `diag` array. The net task stack size is unchanged; watch `net_hwm_words` in timing.

## 5. Verification

Local results are in section 6. ESP-IDF build, flash and bench: **NOT RUN**.

## 6. Results

See the final report for the run commands and their output. Summary is filled in at push time.
