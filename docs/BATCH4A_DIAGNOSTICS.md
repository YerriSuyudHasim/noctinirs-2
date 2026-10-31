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
| `components/nn2_net/nn2_net.c` | `nn2_net_poll` (FRAME loop only) | Counts a failed FRAME `sendto` by class (EAGAIN/EWOULDBLOCK, ENOBUFS, other). Captures `errno` right after `sendto`. Reply path untouched. Same single writer (net task). |
| `firmware/v4-idf61/main/v4_nn2_control.inc` | `control_read_diag` (kind 0, health) | Adds `heap_free_now`, `heap_free_min`, `largest_free_block_now`. Largest-block walk only when `control_state()==NN2_OPTICAL_IDLE`; otherwise `na`. |
| same | `control_read_diag` (kind 1, timing) | Adds `frame_send_errors`, `frame_send_eagain`, `frame_send_enobufs`, `frame_send_other`, `frame_last_errno`. See section 3 for why not health. |
| `firmware/v4-idf61/migration-manifest.json` | three hashes | Only the three hashes the verifier requires for the changed `.inc`, `nn2_net.h`, `nn2_net.c`. No other manifest change. |
| `tools/nn2ctl.py` | `parse_observation` (new), `describe_reply` | Decodes the new keys: integers, `na` -> `None`, `frame_split_ok` (errors == eagain+enobufs+other). Malformed values raise. |
| `config/ram_budget.json` (new) | - | Machine-readable limits: sections, registered statics, compile-time checks, heap values. Status PROPOSED. |
| `tools/check_ram_budget.py` (new) | `parse_map`, `symbol_size`, `check`, `main` | Reads a GNU ld map. Fails on section or symbol over limit, and fails closed if a symbol is missing or ambiguous. |
| `tests/architecture/test_ram_budget.py` (new) | - | Config matches code (constants, `_Static_assert` text, declarations). Gate logic on a **synthetic** map. |
| `tests/architecture/test_batch4a_diagnostics.py` (new) | - | Source contracts: frame counters only in the frame path, largest walk gated to IDLE, keys in the intended reply, boot keys unchanged. |
| `tests/architecture/test_v4_batch1.py` | harness stubs, `test_actual_report_formatter...` | Compiles the real formatter with stubs; now checks IDLE and RUNNING outputs and that each reply is below the 1 349 B buffer. |
| `tests/architecture/test_nn2ctl_diag.py` | 3 new tests | Decode, `na`, split mismatch, malformed input. |
| `docs/RAM_BUDGET.md` (new) | - | Budget tables with source tags, rules, proposed margins. |
| `docs/V4_IDF61_SINGLE_CLOCK.md` | end section | Pointer only. |
| `docs/BATCH4A_DIAGNOSTICS.md` (this file) | - | Record. |

## 2. Health keys and how to read them

Net task only: `nn2_net_get_status()` is called from `control_read_diag`, which is
reached only from `nn2_app_comm_process`, which only `nn2_net_poll` calls. So
the reader and the single writer are the same task. No atomics are needed. If that
call path changes, these reads must become relaxed atomics first.

| Key | Meaning | Cost (ESP-IDF v6.1 source) |
|---|---|---|
| `frame_send_errors` | FRAME `sendto` failures since boot (any class). Equals the three keys below. | - |
| `frame_send_eagain` | Socket send buffer full (EAGAIN/EWOULDBLOCK). Points to the firmware send side. | - |
| `frame_send_enobufs` | Kernel/lwIP buffer exhausted (ENOBUFS). Points to the firmware send side. | - |
| `frame_send_other` | Any other error, or a short send (reported as EIO). | - |
| `frame_last_errno` | errno of the last FRAME failure (0 = none). Signed. | - |
| `heap_free_now` | Free internal 8-bit heap now. | O(registered heaps), no lock: cheap |
| `heap_free_min` | Lowest free internal heap since boot. | O(registered heaps), no lock: cheap |
| `largest_free_block_now` | Largest free internal block. **Only when STOPPED**; `na` while running. | Walks every block under the heap spinlock (`portENTER_CRITICAL_SAFE`): **expensive**, so STOPPED only. |

Reading the D1 result: compare `frame_send_errors` with lost packets from the RAW
capture (`lost_in_breaks / 40`). Mostly `frame_send_eagain`/`frame_send_enobufs`
near that count means loss on the firmware send side. Near zero means loss
happened in the Wi-Fi/network path. `frame_split_ok` in nn2ctl must be `true`.

Old counters keep their meaning. `send_errors` (old) still counts reply and
FRAME failures together. `last_errno` (old) is still shared with the reply path.
A reply failure is therefore `send_errors - frame_send_errors`.

## 3. Deviation from the request: keys not in health

The request asked for all D1 keys in health. Measured with the real formatter
(`test_v4_batch1` harness, representative values):

| Reply (harness values, `test_v4_batch1` formatter) | Length | Limit (`sizeof(diag)-64`) | Margin |
|---|---:|---:|---:|
| health, before Batch 4a | 1 209 | 1 349 | 140 |
| health + heap keys (shipped, 69 B) | 1 278 | 1 349 | 71 |
| health + heap + net + frame keys (not shipped) | 1 461 | 1 349 | **-112 (fails)** |
| timing + frame keys (shipped, 100 B) | 1 259 | 1 349 | 90 |
| timing + frame + net keys (not shipped, 83 B more) | 1 342 | 1 349 | 7 (too thin) |

With all new keys, health is **1 461 B, which exceeds the limit**. At run time
`read_diag` returns 0 and the reply becomes `v4health=0 reason=unavailable`. That
would blind the bench. So I did not ship that. What I did:

- Heap keys (69 B) stay in **health**; they fit with a 71 B margin.
- Frame keys (100 B) go in **timing** (`get_timing`), which has a 90 B margin.
- The `net_*` keys (`net_received`, `net_replied`, `net_invalid`, `net_send_errors`,
  `net_last_errno`, 83 B) are **not emitted**. In timing they would leave a 7 B
  margin, which real counter values could exceed.

Margins use harness values. Real counters with more digits shrink them. Watch for
`v4health=0` or `v4timing=0` after long runs. Those mean the reply overflowed.

**Decision for Owner** (choose one; default is the current state):

1. Keep the current split (recommended for now). Health has heap; timing has frame
   keys. The `net_*` counters can be read later.
2. Add a new read-only opcode `get_netdiag` (append-only registry entry, ADR-0004
   amendment, host test, nn2ctl). Puts all D1/D2 keys in one reply and leaves
   health and timing unchanged.
3. Raise the diagnostic budget. Not recommended: the reply limit is the 1 440 B
   payload and the 1 349 B reserve keeps the ENGINEERING suffix (`mode=`) intact.

## 4. Other changes and checks

- `control_read_diag` also got a guard before the free-heap append: the existing
  `wrote >= cap` return covers the new blocks, so an overflow falls back and does
  not write past the buffer.
- No new allocation, log, ISR, ADS or scheduler code. No timing or stack constant changed.
- Static RAM: +20 B `.bss` (`s_status`). Heap: no change at boot.

## 5. Verification (this environment)

Same commands on basis `02034ef` (separate worktree) and on this branch. Results
are in section 6. Build and bench are **NOT RUN**.

- `tests/architecture/test_batch4a_diagnostics.py`, `test_ram_budget.py`,
  `test_nn2ctl_diag.py`, `test_v4_batch1.py`: pass.
- `tools/verify-v4-idf61.py`: PASS after updating the three manifest hashes.
- Full CI command list: see section 6.

## 6. Results (this environment: Ubuntu, gcc 13.3, cmake 3.28, Python 3.13; not Owner's Windows)

Same four commands on basis `02034ef` (worktree) and on the branch. Exit codes:

| Command | Basis 02034ef | Branch | New failures |
|---|---|---|---|
| `python tools/run_host_tests.py --build <tmp> --evidence <tmp> --failure-controls` | exit 0; 143 tests, 0 failures (plus the deliberate failure-control run: 1 failure, expected) | exit 0; same | none |
| `python tools/verify-v4-idf61.py` | exit 0 (PASS) | exit 0 (PASS, after the three manifest hashes) | none |
| `python -m unittest discover -s tests/architecture -v` | exit 0; 127 tests OK | exit 0; 152 tests OK (+25: 13 RAM budget, 9 Batch 4a, 3 nn2ctl) | none |
| `ctest --test-dir <tmp> --output-on-failure` | exit 0; 2/2 passed | exit 0; 2/2 passed (includes the 61 golden vectors) | none |

Note: the Batch 3 record on Owner's Windows machine had four architecture failures
from an old GCC (C11 options). This environment has a newer GCC, so the basis had
no such failures. The comparison above is basis vs branch in the same environment.

Golden vectors: 61 in `test/golden/optical_frame_vectors.json`, unchanged, and
`nn2_optical_codec_tests` passed on both sides.

Extra local checks:

- Frame counter path: the FRAME loop of `nn2_net_poll` was extracted and compiled
  with stubs (`gcc -Wall -Wextra -Werror -pedantic`, clean). With an ENOBUFS stub
  it set `frame_last_errno=105` (ENOBUFS). The loop structure is unchanged.
- Health and timing reply lengths (harness values): 1 278 / 1 259 B, both under the
  1 349 B buffer limit. Baseline health 1 209 B.

CI on the branch (GitHub Actions): see the final report. Not verified in this file.

NOT RUN: ESP-IDF build of `firmware/v4-idf61`, flash, bench, `check_ram_budget.py`
against a real map, and any on-target timing or heap measurement.
