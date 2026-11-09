# Batch 5A: stop and fault observability

Base: `bd880f5890ea1a72d54e8ab9cc2ed55f002d1382` (integration branch `cursor/n3-single-clock-72a4`).
Scope: observation only. When a control-layer failure or a failed STOP occurs, the device now
reports where it started and which STOP stage made `scan_stop()` return non-OK. Nothing about
shutdown order, return values, cleanup or fault handling changes.

Evidence belongs to the exact commit under review and is recorded in the pull request, not here.
Local host and static checks are never target evidence. The ESP-IDF target behavior, timing, heap
and linker-map delta are measured by the Owner on the bench.

## 1. What is reported

Eight keys are appended, in this order, to the `get_netdiag` text (`nn2ctl.py netdiag`): they follow
`net_last_errno` and precede the existing mode suffix (` mode=ACQ` or ` mode=ENGINEERING ...`) that the
communication reply builder appends after the diagnostic text when it fits.
`get_health` and `get_timing` text is byte-identical to the base.

| Key | Meaning |
|---|---|
| `ctrl_origin` | First control-layer failure: `NONE`, `BOOT`, `START`, `RUNTIME_SCHED`, `RUNTIME_ADS`, `STOP`. |
| `ctrl_error` | Its error code (`esp_err_t`); 0 when `ctrl_origin=NONE`. |
| `failed_stop_stage` | First stage that made `scan_stop()` return non-OK: `NONE`, `SCHEDULER`, `ADS`, `OPTICAL`, `ASYNC_LED`. |
| `stop_stage_error` | The code that stage returned; 0 when `NONE`. |
| `stop_fail_mask` | Cumulative stage failures since the last reset, `0x%02X`. Bits: 0 SCHEDULER, 1 ADS, 2 OPTICAL, 3 ASYNC_LED, 4 FRAME_DRAIN, 5 NETWORK_DRAIN. Bit 5 is defined here, but the current communication build never sets it (see Rules). |
| `stop_attempts` | Number of `scan_stop()` invocations since the last reset (u8, saturates at 255). |
| `stop_last_ok` | `1` when the latest `scan_stop()` returned OK, `0` otherwise, `na` while `stop_attempts=0`. |
| `stop_other_codes` | `none` only when the mask holds no bit other than the one of `failed_stop_stage`; otherwise `unavailable`. |

### Rules

- `failed_stop_stage` is the first non-OK stage among SCHEDULER, ADS, OPTICAL, ASYNC_LED in execution
  order. That order equals the baseline return precedence, so it is exactly the stage whose code
  `scan_stop()` returns. It is never "the last stage executed".
- FRAME_DRAIN and NETWORK_DRAIN stay observed-only: the baseline does not let them affect the return
  value. They never populate `failed_stop_stage` or `ctrl_origin`. The encoding and this build differ: FRAME_DRAIN
  can set bit 4 on the wire. NETWORK_DRAIN is defined as bit 5 in the formatter, but `scan_stop()` passes `ESP_OK`
  for it when `CONFIG_NOCTI_V4_NN2_COMMUNICATION` is set, so bit 5 stays clear in this build's `get_netdiag`
  replies. The host tests exercise bit 5 in the formatter only.
- First failure wins. A later success or a later, different failure never overwrites `ctrl_origin`,
  `ctrl_error`, `failed_stop_stage` or `stop_stage_error`. Later failed stages only add mask bits.
- `stop_attempts` counts actual `scan_stop()` calls, including cleanup retries. It resets at boot and at an
  accepted START. A normal START, run, STOP reports 1 whatever earlier sessions did.
- **Fault cycles can report more than one attempt.** After a control fault (a START failure, a runtime
  monitor fault, or a failed STOP) the control layer can request another STOP by itself. The network task
  polls periodically (a `vTaskDelay(pdMS_TO_TICKS(1u))` delay follows each pass, which does not guarantee an exact 1 ms cadence) and the communication layer calls the existing `off()` once it sees the optical
  FAULT state. `off()` sets the stop request again when the worker is still working or the hardware is not
  marked clean, and the worker then runs `scan_stop()` again. A fault can therefore trigger additional
  attempts, and the number of attempts depends on timing. The frequency of these retries has not been measured.
  `stop_attempts` is 1 in a normal cycle and can be 1 or more in a fault cycle; there is no fixed value there.
  This is existing behavior. Batch 5A only counts it and changes none of it.
- **`stop_last_ok` describes the latest attempt only**, including an automatic retry. If the first STOP failed
  and a later automatic attempt succeeded, `stop_last_ok=1` while `failed_stop_stage` and `stop_stage_error`
  still identify the first failed STOP and `stop_fail_mask` keeps its bits. Do not read `stop_last_ok=1` as
  "no STOP failed". `failed_stop_stage`, `stop_stage_error` and `stop_fail_mask` are the authoritative
  failure fields; `stop_last_ok` only says how the most recent attempt ended.
- **First failed stage versus the cumulative mask.** `failed_stop_stage` and `stop_stage_error` hold one
  stage and one error code: the first STOP failure since the last reset, and they are never overwritten.
  `stop_fail_mask` is a different kind of value: a cumulative OR of the stages that failed in any attempt since
  the last reset, and it can also hold the two observed-only drain bits. A mask bit does not mean that stage was
  the first failure, does not carry an error code, and does not say which attempt set it.
- **Two independent first-failure pairs.** `ctrl_origin` and `ctrl_error` record the first control failure
  (boot, START, runtime monitor, or a STOP failure when nothing earlier was recorded). `failed_stop_stage` and
  `stop_stage_error` independently record the first STOP failure that affects the `scan_stop()` return value.
  A later failed STOP can populate the second pair without overwriting an earlier control failure in the first.
  `stop_fail_mask` stays cumulative and belongs to neither pair.
- A latched failure also sets the sticky control fault, `control_clear` refuses while faulted and START
  needs IDLE. So START cannot be accepted while a failure is latched, and the START reset cannot erase one.
  After a reboot the record is empty.
- The record stays readable in every state after STOP and until the next accepted START resets it.
  **Read it after each STOP and before the next START.**

### Limitations

- Error codes of additional failing stages and of the two drains are not retained. The mask says which
  failed; `stop_other_codes=unavailable` says the codes are missing.
- The mask cannot say which attempt set a bit. With `stop_attempts=1` the whole mask belongs to the latched
  attempt; with more attempts, bits beyond the latched stage can come from any attempt.
- **Coverage.** These fields attribute only control-layer outcomes (boot, start, runtime monitor) and the
  `scan_stop()` stage that returned non-OK. They do not identify where the scheduler recorded its own
  `last_error`, for example 259: that value has about 15 `ESP_ERR_INVALID_STATE` sites in
  `nocti_v4_scheduler.c` and is also set on paths that never reach `scan_stop()`. In the 2026-11-06
  observation (`sched_error=259`, state STOPPED, `fault 0`) these fields are expected to read `NONE`; that is
  negative information, not an attribution. Finding the cause of `FAULT_OPTICAL` 267 or `sched_error` 259 is
  not part of this batch and is not claimed.

## 2. Changes

| File | Change |
|---|---|
| `firmware/v4-idf61/components/nocti_v4_common/include/nocti_v4_stop_diag.h` | New. Pure C policy (no ESP-IDF, no allocation): record, per-attempt report, `report_make`, `commit`, `note_ctrl`, `reset`, `format`. `_Static_assert` of 16 B. |
| `firmware/v4-idf61/main/app_main.c` | `scan_stop()` takes `nocti_v4_stop_report_t *report` (nullable) and fills it from results it already holds. Call order, the return chain and cleanup are unchanged. |
| `firmware/v4-idf61/main/v4_nn2_control.inc` | `s_control_diag`; latch writes inside critical sections that already exist (boot fault, start failure, runtime monitor, STOP commit); reset in the accepted-START section; `control_diag_snapshot()`; netdiag appends the eight keys. |
| `firmware/v4-idf61/migration-manifest.json` | Hashes of the changed bridge files and the new header. |
| `test/host/test_v4_stop_diag.c`, `test_main.c`, `nn2_test.h`, `CMakeLists.txt` | Unity suite for the production header. |
| `tools/run_host_tests.py` | The zero-tests control also removes the new suite. |
| `tests/architecture/test_batch5a_stop_diag.py` | Runs the real `scan_stop()` and control functions natively with injected failures, plus source-structure and tool-compatibility tests. |
| `tests/architecture/test_diag_worst_case.py`, `test_v4_batch1.py` | Preamble stub for `control_diag_snapshot` and the new header; worst-case netdiag assertions. |

Unchanged by construction: shutdown order, `scan_stop()` return precedence, cleanup, fault handling,
`clear-fault` refusal, `get_health` and `get_timing` text, timing constants, task priorities and cores,
I2C speed, pins, watchdog and safety, partitions, sdkconfig, `K_target=10`, `K_min=8`, port 3334, `reference/`.
No allocation was added on the running data path and no PSRAM is used.

## 3. Concurrency

- Every write to `s_control_diag` happens inside an `s_control_lock` critical section that already existed for
  the fault flag at the same site. The STOP commit shares the critical section that clears `s_control_working`.
- The reader (`control_diag_snapshot`) takes `s_control_lock` once for a 16 B copy. It runs in the net task from
  `control_read_diag` kind 3. This is the only new critical section. `s_control_lock` is a spinlock that masks
  interrupts on the calling core, so its cost is a short copy; it is not measured here.
- `scan_stop()` runs in the control worker with no lock held, and fills a worker-stack record that is committed
  later under the lock. A reader can therefore see the record from before a STOP in progress, never a torn one.

## 4. RAM and runtime (preliminary)

| Quantity | Value | Class |
|---|---|---|
| `sizeof(nocti_v4_stop_diag_t)` | 16 B | Measured with Xtensa GCC 15.2.0 and with the host GCC (`_Static_assert` plus object-size probe) |
| `sizeof(nocti_v4_stop_report_t)` | 8 B | Measured, same method |
| Existing `s_control_error` | 4 B, kept | Source |
| Linker-map delta | Owner build only | Not measured by this batch's host checks |
| Netdiag text | worst case 188 B appended (552 B total against the 1,349 B capacity) | Measured by the host worst-case test |
| Heap | no new allocation | By construction |

The 16 B budget is a design target and an Owner decision. `config/ram_budget.json` is not changed here.
Proposed entry, for the Owner to approve or reject:
`{"label": "main.s_control_diag", "section": "bss", "name": "s_control_diag", "object_contains": "main", "limit_bytes": 16, "declared_in": "firmware/v4-idf61/main/v4_nn2_control.inc", "declaration": "static nocti_v4_stop_diag_t s_control_diag;", "source": "Batch 5A"}`.

## 5. Tests

Host/unit class unless noted; none of them runs on the target.

- `test_v4_stop_diag.c` (Unity, 19 tests): each return-affecting stage alone; drain-only; drain plus a
  return-affecting failure; all 64 failure combinations against an independent model of the baseline return
  chain; first-error preservation across repeated cleanup; START failure then STOP failure; first-wins; reset;
  per-session `stop_attempts`; saturation at 255; reserved mask bits; exact text; text at maximum field widths
  (188 B); all-or-nothing behavior on a short buffer.
- `test_batch5a_stop_diag.py`:
  - `scan_stop()` extracted from `app_main.c` and run with stubbed stages: 512 combinations of stage failures,
    stage activity and communication config; call order, return value, report and frozen drain results are
    compared with the baseline model.
  - `control_start`, `control_clear`, `control_note_boot_fault` and `control_diag_snapshot` extracted from the
    include and run against its real static declarations: reset on accepted START, no reset when START is
    refused, boot origin, first-wins, one critical section per snapshot.
  - Source-structure checks for the worker sites, lock coverage, the unchanged `scan_stop()` text and a hash of
    the unchanged `get_health` and `get_timing` formatter code.
  - `nn2ctl.parse_diag` accepts the appended keys.
- `test_diag_worst_case.py`: the real `control_read_diag` at worst case.

## 6. Owner bench procedure (proposed; not execution-ready)

This section prepares the bench. It does not authorize any physical test, flash, energized work or device command.
Each run needs a separate Owner authorization (A3) that names target, action, time window and stop conditions, a
procedure reviewed by the Hardware Specialist (Agent 04) and the RTOS Specialist (Agent 05), and the pre-bench
decisions below. **Every flash needs its own A3 authorization**, the base-image flash included.

Status tags used below: **UNKNOWN** = no local evidence; **PENDING OWNER** = only the Owner can supply or decide it;
**PROPOSED** = a suggestion in a record, not decided. No default is assumed for an UNKNOWN or PENDING OWNER item. If an
item is missing at the time of the run, the run does not start.

### 6.1 Bench preconditions (Owner inputs; nothing here is a default)

| # | Item | Status | What the Owner must record |
|---|---|---|---|
| a | Board ID and assembled revision | UNKNOWN, PENDING OWNER | The board used, and that the same board is used for every series. It is not inferred from `reference/` or from earlier bench records, which did not record it. |
| b | Fixture, enclosure, optical containment, placement | UNKNOWN, PENDING OWNER | Fixture identifier or description, containment, and the placement held constant for all series. |
| c | Emitter conditions | UNKNOWN, PENDING OWNER | Exposure conditions for the `--led` runs (for example whether any skin, eye or human exposure is excluded). Thermal and optical limits stay open; none is invented here. |
| d | **Approved emitter current** | UNKNOWN, PENDING OWNER | The current the Owner approves for this bench test. See "Current" below. |
| e | Operator | UNKNOWN, PENDING OWNER | Name or role of the person who runs the procedure and of the person who can remove power. |
| f | Power sources, including battery | UNKNOWN, PENDING OWNER | Every source feeding the board (USB, supply, cell). Whether a cell is fitted. Battery work is an A3 activity (governance policy). |
| g | Accessible power-removal method | UNKNOWN, PENDING OWNER | The physical method that removes all energy from the board (and the emitters), who operates it, and that it is within reach during every run. `safe-off` is a software command and is not this method. |
| h | Physical stop conditions | UNKNOWN, PENDING OWNER | Observable physical triggers that end a run. Candidates raised in the Hardware Specialist's review, to be confirmed or replaced by the Owner: unexpected emission after STOP, abnormal heat or odour, a supply anomaly, a disturbed fixture. No thresholds are set here. |
| i | Cleanup | UNKNOWN, PENDING OWNER | Confirm or amend the cleanup in section 6.6. |
| j | Network mode and client | UNKNOWN, PENDING OWNER | SoftAP or station, one connected client, no other traffic. The tools default to host `192.168.4.1`, port 3333 (`tools/nn2ctl.py`, `tools/nn2_raw_capture.py`); that is a tool default, not a confirmed bench setting. |
| k | Flash step and image identity | PENDING OWNER (A3) | The flash itself is outside this document. The images to identify are the ones in the existing build evidence (see "Evidence status"). Emitter energy must stay unavailable through flash and the automatic reboot. |
| l | Serial boot capture | PENDING OWNER (needed only if PB-2 adopts the heap margins) | Whether a serial monitor is attached from boot so the boot `NN2_HEAP` lines are saved. |

**Current.** The firmware default is 30 mA per slot with a 50 mA cap (`components/nn2_core/include/nn2_core_optical.h`:
`NN2_OPTICAL_DEFAULT_MA 30`, `NN2_OPTICAL_MAX_MA 50`; `tools/nn2ctl.py` validates against the cap and warns above 30 mA).
These are firmware defaults and proposals. **They are not an Owner-approved bench current.** ADR-0005 states that the
bring-up record does not qualify 30 mA exposure. The Owner must name the approved current. If the Owner approves the
firmware default, the proposed way to use it is no `--current-ma` and no `set-led-current`, with `get-config` read
before each block (section 6.3). If the Owner approves a different value, the tool path that writes it must be named
and reviewed first; it is not covered here.

### Evidence status (kept separate)

- **Verified local build.** Baseline and candidate were built with the sdkconfig whose SHA-256 is
  `26a7b9aaa18465927205ca2c65311bc1664e387f0da32a64eb37b2b9ac7828af`, ccache disabled
  (`review/owner-config-build/OWNER_CONFIG_BUILD_REPORT.md`, reviewed in `CODEX_IR_OWNER_CONFIG_BUILD.md`). This is
  existing evidence, not a check run for this revision. It shows a static `.dram0.bss` change of +16 B (165,208 to
  165,224 B), accounted for by `s_control_diag`.
- **That is not total runtime RAM sufficiency.** It says nothing about stack, heap at runtime or timing, and it is not
  PB-6 acceptance.
- **Whether that exact hashed sdkconfig is the intended bench configuration is a separate Owner confirmation
  (PB-0).** Matching bytes are verified; the intent is PENDING OWNER.
- Build artifact identities from that evidence, for the Owner's flash record only: base image
  `c8114cb3780e4a2f33319884badd8594ea3c6a6fc34e7c7a882e65fc38a0485a`, candidate image
  `5b50b95f771b90c4716017511a19c837e30bc118bce1b2c1b2345d46ed8904a0` (`.bin` SHA-256).

### Pre-bench decisions (open)

- **PB-0** Is the sdkconfig with SHA-256 `26a7b9aaa18465927205ca2c65311bc1664e387f0da32a64eb37b2b9ac7828af` the intended bench configuration? (Added by revision 4. It does not renumber PB-1 to PB-6.)
- **PB-1** Heap drift rule: is a base-build control series (the same cycles on `bd880f5`, same board and placement) required?
- **PB-2** Are the PROPOSED margins in `docs/RAM_BUDGET.md` (2,048 B boot largest-block margin, 4,096 B `heap_free_min`) adopted for this test? Until the Owner says so they are not limits.
- **PB-3** Emitter and fixture conditions for the `--led` runs (current, enclosure, no skin contact, power removal), defined by Agent 04. The tool also guards real-LED use behind the approved bench procedure.
- **PB-4** The regression reference below uses "~" values from earlier records. Does the Owner accept them as limits or give exact numbers?
- **PB-5** Is 10 ACQ cycles plus 10 ENGINEERING cycles accepted as bounded evidence?
- **PB-6** RAM ceiling for `s_control_diag` (proposed: at most 16 B of `sizeof`), then the measured map delta.

Status of PB-0 to PB-6: **PENDING OWNER** (none decided). The decision sheet is in
`review/docs-revision4/OWNER_DECISION_SHEET_BATCH5A.md`.

### Baseline measurements: which conclusions need them (PB-1)

| Conclusion | Needs a same-session base series? |
|---|---|
| STOP diagnostics are as expected in each candidate cycle (section 1, step 8) | No |
| The candidate's absolute `timing` values, `ctrl_hwm_words`, `net_hwm_words` and heap numbers, recorded | No (records only) |
| "Candidate stack headroom is not lower than the base" (`*_hwm_words` comparison) | **Yes** |
| "Candidate timing is not worse than the base" (`spi_max`, `service_max`, `lag_max`, `late_max`, `sched_deferred_max`) | **Yes**, unless the Owner gives exact limits under PB-4 |
| Heap drift judged against the base series spread | **Yes** |
| Heap drift, candidate only | No: it is flagged for Owner review only; there is no spread threshold |
| The +16 B static BSS delta | No: it comes from the existing builds |

If PB-1 is "no", or the base series is declined or not run, every comparison marked **Yes** is **UNAVAILABLE** and is
reported as such; none is replaced by a guess. If PB-1 is "yes", the base series uses the same board, fixture,
placement, sdkconfig, network and cycle plan, the order (base first or candidate first) is recorded, and it adds a
second flash that needs a **separate A3 authorization**. Nothing in this document authorizes that flash.

### ADS error coherence (RTOS R1 / Grok F6)

Revision 4 carried this as an open source finding. The local R1/F6 delta publishes ADS
lifecycle and first-fault history together for the runtime monitor. This has host and
static build evidence and awaits review of the code delta. If a bench observation reports
`RUNTIME_ADS` with a zero or unexpected error, preserve the raw result for investigation.
Fault injection requires its own approved procedure.

### 6.2 Command reference (checked against local source; none executed against a device)

Commands are the existing tools, run from the firmware checkout: `python tools/nn2ctl.py <command> [options]` and
`python tools/nn2_raw_capture.py [options]`. Their `--help` output was run locally without a device; the rest was read
from the source (`tools/nn2ctl.py`, `tools/nn2_raw_capture.py`, `components/nn2_app/src/nn2_app_comm.c`,
`firmware/v4-idf61/main/v4_nn2_control.inc`).

| Command | Verified from source | Notes and UNVERIFIED points |
|---|---|---|
| `nn2ctl.py info`, `status`, `get-config`, `netdiag`, `health`, `timing` | `COMMANDS` table; each prints one JSON object on stdout; `netdiag`, `health`, `timing` put the parsed keys under `diag` | UDP; default `--host 192.168.4.1 --port 3333 --timeout 0.5 --retries 2`. Errors go to stderr with exit code 1. |
| `nn2ctl.py set-mode --mode ACQ` or `ENGINEERING` | Requires `--mode`; firmware replies INVALID_STATE unless the session is STOPPED, and the tool then reports "set-mode rejected: session must be STOPPED"; BUSY or PRECONDITION_FAILED (RAW buffer memory) are possible replies | The mode does not reset the Batch 5A diagnostic record (only boot and an accepted START do). |
| `nn2ctl.py run --rate <2,4,5,8,10> --seconds <n> --led` | START, heartbeat loop every 0.5 s, then STOP, then up to 2 s of `status` polling until STOPPED; exit code 0 only if the final state is STOPPED. `--led` is required when `info` shows `led=REAL_BRINGUP`, and refused when it does not. With REAL_BRINGUP the tool reads `get-config`, rejects values above the firmware cap, warns above 30 mA, and sends `set-fs` for `--rate` | It does not read `netdiag`, `health` or `timing`. `--current-ma` would write all 20 channels and is not used by this procedure. Real-device behavior, UDP loss and timing are UNVERIFIED. |
| `nn2ctl.py safe-off`, `clear-fault` | Opcodes 0x20 and 0x21 exist in the tool and in `nn2_app_comm.c` | `safe-off` ends the session in software. It is not physical power removal. Whether either command alters the diagnostic record is UNVERIFIED; the procedure reads the record before using them. |
| `nn2_raw_capture.py --seconds <n> --rate 10 --output <prefix>` | `--rate` accepts only 10; `--output` is a prefix and the script writes `<prefix>.bin` and `<prefix>.csv`; it requires `;mode=ENGINEERING` in `info`; it sets `--rate`, starts, heartbeats, stops and confirms STOPPED (up to 5 s) | The script has **no `--led` option and does not read `get-config`**. The firmware START path in `v4_nn2_control.inc` (emitter gate, per-channel IREF) is the same in both modes, so an ENGINEERING capture is to be treated as an energized run at the configured current under the same 6.1 conditions. This is a source reading; it is UNVERIFIED on a device. |
| `info` mode text | `;mode=ACQ` or `;mode=ENGINEERING;gate_config_id=0;frame_flags=4` is appended to `info` (`nn2_app_comm.c`); the `info` text can carry further fields after it | Check that `info` **contains** the expected `;mode=` text. Do not rely on "ends with". `health`, `timing` and `netdiag` carry the same mode with a leading space instead of a semicolon. |
| Saving output | `python tools/nn2ctl.py netdiag > <file>` captures the JSON | Redirect only stdout. Name the file with the cycle number and a UTC time. The redirection syntax itself is shell behavior and was not exercised against a device. |

### 6.3 Procedure

0. **Gate.** Do not begin unless section 6.1 is complete, PB-0 to PB-6 are recorded (or recorded as declined), the A3
   authorization names target, action, window and stop conditions, and Agent 04 has re-reviewed this procedure. Any
   missing input ends the procedure before power is applied. Record the evidence fields: repository and firmware
   SHA (baseline plus the uncommitted patch, with the patch SHA-256), flashed `.bin` SHA-256, sdkconfig hash, tool
   versions, board and fixture identifiers, UTC time, operator, command or procedure, result
   (`PASS`/`FAIL`/`BLOCKED`/`NOT RUN`), artifact SHA-256 and deviations.
1. **Record and verify.** Run `info` and `status`. Confirm `info` contains `baseline=V4_IDF61_NN2_CTRL` and
   `;mode=ACQ`. If it shows ENGINEERING, run `set-mode --mode ACQ` while STOPPED and read `info` again.
   Before every energized block (steps 2, 3, 5, 6, 7) run `get-config` and confirm that the 20 current values equal
   the Owner-approved current from 6.1 d. If they do not, stop; do not change them with this procedure.
2. **Initial ACQ functional checks (30 s each).** `run --rate 2 --seconds 30 --led`, then `--rate 4`, then
   `--rate 10`. After each STOP apply section 6.4.
3. **Initial ENGINEERING functional check.** While STOPPED: `set-mode --mode ENGINEERING`; read `info` and
   confirm `;mode=ENGINEERING`. Run `nn2_raw_capture.py --seconds 30 --rate 10 --output <prefix>`. After STOP apply
   section 6.4. (Energized run: see the command table.)
4. **Return to ACQ.** While STOPPED: `set-mode --mode ACQ`; read `info` and confirm `;mode=ACQ`. **Do not continue
   until `;mode=ACQ` is confirmed.** (`set-mode` is refused while a session runs.)
5. **Regression, 600 s.** Read `info` again and confirm `;mode=ACQ`, then `run --rate 10 --seconds 600 --led`. After
   STOP, apply section 6.4 (read after STOP and before the step 6 START). The "~" values below are
   **expectations from earlier records, not acceptance limits (the `<=` and `~` mark expected values, not limits)**: `fault=0`, `edge_miss=0`, `adc_missed=0`, `gap=0`,
   `crc_bad=0`, `spi_errors=0`, `spi_max <= ~60 us`, `service_max <= ~100 us`, `lag_max ~33`, `late_max <= 2`,
   `sched_deferred_max` not clearly higher. Record every measured value as a measurement. A pass or fail limit exists
   only where the Owner records one under PB-4, as "limit, source, date". Otherwise the entry reads
   "measured, no Owner limit". A comparison with a base series exists only if the base series was run (see above).
6. **Repetition, ACQ block.** Confirm `;mode=ACQ`. Ten cycles of `run --rate 10 --seconds 30 --led`, with section 6.4
   after **every** STOP and before the next START. Save each cycle under its cycle number.
7. **Repetition, ENGINEERING block.** While STOPPED: `set-mode --mode ENGINEERING`, confirm `;mode=ENGINEERING`. Ten
   cycles of `nn2_raw_capture.py --seconds 30 --rate 10 --output <prefix_n>`, with section 6.4 after every STOP and
   before the next START. Then `set-mode --mode ACQ` and confirm `;mode=ACQ`.
8. **Per-cycle expectation.** `ctrl_origin=NONE ctrl_error=0 failed_stop_stage=NONE stop_stage_error=0
   stop_fail_mask=0x00 stop_attempts=1 stop_last_ok=1 stop_other_codes=none`, `fault=0`, state STOPPED. In
   ENGINEERING also `raw_dropped_samples=0` and `lost_in_breaks / 40 == delta(frame_send_errors)` (the
   established Batch 4c criteria; no numeric loss limit is approved). These values are for a normal cycle only.
   This step interprets STOP outcomes. To interpret the original control fault, read `ctrl_origin` and
   `ctrl_error`: a START or runtime fault followed by a successful cleanup can leave `failed_stop_stage=NONE`,
   `stop_stage_error=0` and `stop_fail_mask=0x00` while `ctrl_origin` is not `NONE`. In a fault cycle
   `stop_attempts` may be more than 1, depending on timing, and `stop_last_ok` belongs to the latest, possibly
   automatic, attempt (section 1, Rules). Judge the STOP outcomes by the first failed stage and its error
   (`failed_stop_stage`, `stop_stage_error`) together with the cumulative `stop_fail_mask`, keeping the two apart.
   Preserve any zero or unexpected `ctrl_error` reported with `ctrl_origin=RUNTIME_ADS` (see above).
9. **Stack headroom (new code touches two tasks).** From each `timing` reply record every `*_hwm_words` key, at least
   `ctrl_hwm_words` (control worker, runs `scan_stop()` and commits the record) and `net_hwm_words` (net task, copies
   the snapshot). The key names say "words"; confirm the SDK unit before converting to bytes. These are recorded
   candidate values. A comparison with the base is available only if the base series was run; otherwise it is
   **UNAVAILABLE**. No headroom limit is invented; the Hardware and RTOS reviewers judge any difference.

### 6.4 Post-STOP evidence capture (before another START, a clear, a safe-off or a reboot)

**What each signal means.**

| Signal | Source | What it establishes | What it does not |
|---|---|---|---|
| ACK | `stop` reply with `error=OK` | The device accepted STOP | That STOP finished or that emitters are off |
| Reported state | `status` or heartbeat `state` going STOPPING to STOPPED; `run` exits 0 only on STOPPED | The control layer finished its STOP work: the worker commits the diagnostic record and then sets state IDLE in the same critical section (`v4_nn2_control.inc`), so once STOPPED is reported the record includes this STOP | A physical measurement |
| Diagnostic record | `netdiag` keys `ctrl_origin`, `ctrl_error`, `failed_stop_stage`, `stop_stage_error`, `stop_fail_mask`, `stop_attempts`, `stop_last_ok`, `stop_other_codes` | Software-reported STOP and control-fault outcome for the current session | Emitter state in the world |
| Physical emitter-off | An Owner-defined check (PENDING OWNER, 6.1 and PB-3) | Only an instrument or inspection can show it. The host output cannot. The Hardware Specialist's review records that OE# is hardwired low, so no hardware output disable exists | |

**Reset semantics (source).** The record is zeroed at boot and at an accepted START (`nocti_v4_stop_diag_reset` in
`control_start`). A refused START does not zero it. Reading it does not change it. `set-mode` does not touch it.
Whether `safe-off` or `clear-fault` changes it is UNVERIFIED, so the capture below comes first. A reboot, power cycle
or flash erases it. **The first START after a cycle erases that cycle's evidence.**

**Capture sequence, after every STOP in steps 2, 3, 5, 6 and 7.**

1. Confirm the tool exited 0 and the last printed state was STOPPED. If not, capture anyway, then apply the stop rule.
2. Immediately run, saving stdout to cycle-numbered files with a UTC time:
   `python tools/nn2ctl.py netdiag`, `... health`, `... timing`, `... status`.
   (`largest_free_block_now` is a number only while STOPPED.) For ENGINEERING cycles also keep the `.bin` and `.csv`
   and the `nn2_raw_capture.py` summary line.
3. Only after the files are saved, check the expectations in step 8 and the Owner-defined physical check (if any).
4. Only then start the next cycle, `clear-fault`, `safe-off`, change power or reboot.
5. After a fault, a failed STOP or a non-zero exit, read `netdiag` a second time a short interval later and save both
   reads with their times: an automatic cleanup retry can commit another attempt after the first read.

Expected observations are the normal-cycle values in step 8. Anything else is handled by the stop rule, not
reinterpreted.

### 6.5 Stop rule

Stop the series at the first deviation: any fault or FAULT state; `ctrl_origin` not `NONE`; `failed_stop_stage`
not `NONE`; `stop_fail_mask` not `0x00`; `stop_attempts` not 1; `stop_last_ok` not 1; a breach of an Owner-recorded
limit (PB-4); `raw_dropped_samples` above 0; a Wi-Fi station disconnect during a series; a mode that is not the
expected one; a current that is not the approved one; a heap flag the Owner does not accept; **or any physical stop
condition the Owner defined in 6.1 h**. If a physical condition occurs, remove power by the method in 6.1 g first.
Then:

1. Do not reboot and do not continue the series.
2. Save unchanged the full `netdiag`, `health`, `timing`, `status` and `info` text, the serial log if a monitor
   was attached, the `.bin` and `.csv` of the current cycle, and the cycle number (apply 6.4 if power is still on). A
   reboot clears the diagnostics, so the first failure's evidence must be saved first. If power had to be removed
   before capture, record that the software evidence was lost.
3. Report to the Owner. At most one reboot per series, only after the evidence is saved and the Owner decides.
   A series interrupted by a reboot is closed, not resumed. Repeated reboots to reproduce are not permitted.

### 6.6 Cleanup (proposed; Owner to confirm or amend, 6.1 i)

1. Complete 6.4 for the last cycle. 2. Confirm state STOPPED and `;mode=ACQ`. 3. `safe-off` (software only).
4. Remove power by the method in 6.1 g and confirm it is removed. 5. Archive all text, `.bin`, `.csv` and serial logs
with SHA-256 and UTC time, outside Git unless the Owner decides otherwise. 6. Record the image left on the board and
the result (`PASS`/`FAIL`/`BLOCKED`/`NOT RUN`) with deviations and unrun items.

### Heap rule (no numeric threshold is invented)

- After every STOP and before the next START record `heap_free_now`, `heap_free_min` and
  `largest_free_block_now` (a number only while STOPPED) from `netdiag` (section 6.4 captures them).
- No heap limit is approved. If the Owner adopts the PB-2 margins, breaching them fails the run. Otherwise the
  numbers are recorded and compared without an absolute pass or fail. The boot largest-block margin needs the boot
  `NN2_HEAP` lines from a serial capture started at boot (6.1 l).
- `heap_free_min` is the lowest value since boot and can only fall, so it is a floor, not a drift signal. The
  drift signals are idle `heap_free_now` and `largest_free_block_now` per cycle.
- Normal variability: Batch 4c showed `heap_free_min` between 9,948 B and 11,680 B across ENGINEERING runs and
  idle `heap_free_now` of about 20.3 KB. A single low value is not a leak.
- **Unexplained downward drift** is flagged when, within one 10-cycle block, the idle `heap_free_now` series (or
  the `largest_free_block_now` series) falls at every one of the 9 successive steps. If a base series was run, the
  flag also requires the total fall to exceed the spread (max minus min) of that base series. Without a base series
  the same monotone fall is flagged for Owner review only, and no spread threshold exists. A flag is not an automatic
  failure; it triggers the stop rule. A leak is not claimed from 10 points.
- Keep one station connected with no other traffic and the same placement. Mark the series interrupted if the
  station reconnects.

### Evidence class

On-target bench, bounded regression evidence for one board and one build. It is not reliability qualification.
The proposed count (10 ACQ plus 10 ENGINEERING cycles, plus the 30 s and 600 s runs) provides **limited evidence**
only. That count is a proposal. Whether the Owner accepts it as bounded evidence is PB-5 and is not decided here. The
result reads "no deviation observed in these cycles". Unrun items are listed as NOT RUN.

## 7. Rollback

Revert the change. There is no persistent state, partition, bootloader, OTA, secure-boot or eFuse effect.
The diagnostics are empty after every reboot.
Bench cleanup is a separate matter and is in section 6.6.

## 8. Documentation revision history (local)

| Revision | Change |
|---|---|
| Review candidate | Original Batch 5A documentation (patch SHA-256 `5acbee58...a079`). |
| Docs revision 1 (local) | Addresses independent review finding F1. Documentation only. Superseded by revision 2; its artifacts are kept unchanged. |
| Docs revision 3 (local) | Addresses the Codex independent review of revision 2: periodic network poll wording, bench step 8 scoped to STOP outcomes with `ctrl_origin` and `ctrl_error` for the original control fault, the two independent first-failure pairs, the position of the eight keys before the mode suffix, and the difference between the encoded bit 5 and this build, which never sets it. Documentation only. No firmware, test or tool file changed. Needs a focused re-review of the delta; it is not an approval. |
| Docs revision 4 (local) | Procedure and documentation revision only, answering the Hardware Specialist's bench-readiness review: section 6 now lists the Owner inputs that must be confirmed (board, fixture, emitter conditions, approved current, operator, power sources, power removal, physical stop conditions, cleanup, network), keeps 30 mA as a firmware default and not an approved current, adds PB-0 without renumbering PB-1 to PB-6, a command reference checked against local source, a post-STOP capture section that separates ACK, reported state and physical emitter-off, the baseline-dependent conclusions, the open ADS error-coherence item, and the limits of the proposed cycle count and of the +16 B static result. No firmware, test or tool file changed and nothing was run on a device. It grants no PB decision and no authorization. Needs an Agent 04 procedure re-review. |
| Docs revision 2 (local) | Removes the earlier typical-count wording and any implication that retry frequency was measured: a fault can trigger additional attempts and the count depends on timing. Separates the first failed stage and its error from the cumulative `stop_fail_mask`. Documentation only. No firmware, test or tool file changed. Needs a focused review of the delta; it is not an approval. |
