# ADR-0003: RTOS Task Topology for NoctiNIRS 2

| Field | Value |
|---|---|
| ID | ADR-0003 |
| Title | ESP32-S3 dual-core FreeRTOS task topology (optical acquisition + Wi-Fi command path) |
| Status | **Accepted** (unchanged by rev 2). Owner Accept `confirm_accept_write`, 2026-10-17 Asia/Shanghai, landed via PR #29 squash `6309c06bdedd01bd2430ccd62b5c4604b34f9d8a` after Agent 07 IR APPROVE on head `cbb0c9b36a3825a9bd8c1bbd756c32e6e41c6059`. Rev 2 amends the Accepted inventory under Owner write authorization via Nori, 2026-10-17 Asia/Shanghai ([#25](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226379710)): sole placeholder `nn2_ads` superseded by `nn2_ads131` and `nn2_ads1299`. This amend still needs a fresh Agent 07 IR on its head SHA before Owner merge. Firmware not implemented. Class H / flash / WCET **NOT RUN**. **Accepted** does not by itself authorize implementation, flash, or Class H. |
| Date | 2026-10-17 (rev 2; dual-ADS amend); 2026-10-29 (rev 3; V4 image order). Status remains **Accepted**. |
| Issue | [#25](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25) |
| Author role | Architect (Agent 02) and RTOS Specialist (Agent 05); file recorded from the #25 consensus under Owner write authorization |
| Independent reviewer | Agent 07 — APPROVE on Proposed PR #27 head `e8dfbe852e1d920fce476a90ce3a495f4b6ed33c` and on Accept PR #29 head `cbb0c9b36a3825a9bd8c1bbd756c32e6e41c6059` ([#29](https://github.com/YerriSuyudHasim/noctinirs-2/pull/29#issuecomment-6225890253)). This dual-ADS amend still requires a fresh Agent 07 independent review on this pull request’s head SHA before Owner merge. The reviewer must not be an author of this ADR. |
| Approver / merge | Project Owner `YerriSuyudHasim` only |
| Input commit | This amend starts from Accepted `main` @ `6309c06bdedd01bd2430ccd62b5c4604b34f9d8a` (squash of PR #29). Proposed text base `791e35b79ba8cef343280d76973fee21d3b41e42`. Proposed land SHA `27e33d874387579986d781d547c61d8441d3631b` (squash of PR #27). |
| Evidence class of this document | Static (Class S design review only). Class H / flash / WCET: **NOT RUN** |

> This ADR describes a *target* task topology. Nothing in it is implemented, and **Accepted** does not implement firmware. **Status: Accepted** (2026-10-17; rev 2 amends the inventory and does not flip Status). Accepted means this topology contract is the approved design baseline for later firmware issues. It does **not** by itself authorize firmware implementation, flashing, Class H, hardware tests, merges, releases, or tags. Affinity, literal priority, stack, queue, and timing-budget numbers stay **OPEN** until Class H, including the relative priority between `nn2_ads131` and `nn2_ads1299`. Class H / flash / WCET remain **NOT RUN**. Where this document calls a placement or a numeric cell **Proposed** or **OPEN**, that cell is not a frozen pin. Historical V2/V3/V4 constants are **evidence only**. A successful build is not hardware proof. A fresh Agent 07 independent review of this dual-ADS amend’s head SHA is still required before Owner merge.

## 1. Context

### 1.1 Problem

NoctiNIRS 2 must run optical acquisition and a Wi-Fi command path on an ESP32-S3 with dual-core FreeRTOS (ESP-IDF v6.1 and its bundled FreeRTOS), inside the clean-architecture boundaries of ADR-0001 and the optical timing contract of ADR-0002.

ADR-0002 (Accepted) owns **what** an optical frame is: the allowed frame-rate set, ON/OFF geometry, averaging count \(K\), and absolute deadlines. It does not own **who runs where** under FreeRTOS. Without a single public topology, later driver pull requests would invent conflicting tasks, priorities, core pins, ISR contracts, and watchdog rules.

This ADR is that contract: task inventory and lifecycle, Core-1 priority **ordering** (numbers open), core affinity (final value open), ISR-to-task handoff, bounded waits, TWDT policy, M1 transport placement, and the split between Class S/B/U evidence and Class H evidence.

### 1.2 Current product state (verified at `791e35b`)

- Active firmware remains the 0.1.0 scaffold. This ADR adds no tasks, drivers, or build inputs.
- ADR-0001 (`docs/adr/0001-clean-architecture-boundaries.md`) is **Proposed** on `main`. Its dependency rule still binds this ADR: FreeRTOS, ESP-IDF, GPTimer, and TWDT live only in adapters and the composition root.
- ADR-0002 (`docs/adr/0002-fnirs-optical-acquisition-timing.md`) is **Accepted** on `main` via [#24](https://github.com/YerriSuyudHasim/noctinirs-2/pull/24) (squash `791e35b`). Optical geometry is not restated here.
- Issue [#23](https://github.com/YerriSuyudHasim/noctinirs-2/issues/23) recorded the sequencing **A(+E) → C → D → B (Class H)** before any Core-1 priority ladder is frozen. This ADR is step **A** (with **E**).
- Class H (on-target distributions, WCET, TWDT under load) is **NOT RUN**. No flash is authorized by this document.

### 1.3 Historical references (evidence only)

`reference/v2`, `reference/v3`, and `reference/v4` are read-only snapshots. They are not NN2 requirements and are not copied into active firmware. Agent 05’s Class S outline on #25 records a V4 ladder. At `791e35b` this ADR re-checked only the priority macros and the absence of `esp_task_wdt` under `reference/v4`. Other V4 depths below are cited from that outline and are **not** re-audited here.

| Item | Class S evidence (do-not-lift) | Where |
|---|---|---|
| LED task priority 22, core 1 | `ASYNC_TASK_PRIORITY` 22, `ASYNC_TASK_CORE` 1 | `reference/v4/components/nocti_v4_async_led/nocti_v4_async_led.c` |
| Scheduler priority 21, core 1 | `NOCTI_V4_SCHEDULER_PRIORITY` 21, `NOCTI_V4_SCHEDULER_CORE` 1 | `reference/v4/components/nocti_v4_scheduler/include/nocti_v4_scheduler_config.h` |
| ADS priority 20, core 1 | `NOCTI_V4_PRIO_ADC` 20, `NOCTI_V4_CORE_REALTIME` 1 | `reference/v4/components/nocti_v4_common/include/nocti_v4_ads_config.h` |
| Network priority 4, core 0; stack macro 6144 | `NOCTI_V4_NETWORK_TASK_PRIORITY` 4, `NOCTI_V4_NETWORK_TASK_CORE` 0, `NOCTI_V4_NETWORK_TASK_STACK_BYTES` 6144 | `reference/v4/components/nocti_v4_network/include/nocti_v4_network.h` |
| Scheduler stack macro 6144 | `NOCTI_V4_SCHEDULER_STACK_BYTES` 6144 | same scheduler config header |
| Frame consumer priority 5, core 0 | `FRAME_TASK_PRIORITY` 5, `FRAME_TASK_CORE` 0 | `reference/v4/components/nocti_v4_optical/nocti_v4_optical.c` |
| Dark diagnostic priority 12, core 0 | `NOCTI_V4_PRIO_DARK_DIAGNOSTIC` 12, `NOCTI_V4_CORE_DARK_DIAGNOSTIC` 0 | ADS config header |
| TWDT | **0** `esp_task_wdt` hits under `reference/v4` | Agent 05 Class S skim; re-checked at this input commit |

**Do-not-lift.** V4 priorities **22 / 21 / 20** are Class S evidence of an ordering hypothesis. They are **not** NN2 constants. Stack figure 6144 appears in macros named `*_STACK_BYTES`. Agent 05’s outline table placed 6144 under a column titled “Stack (words)”. NN2 stack **depth and unit** stay **OPEN**. Do not adopt 6144 as either words or bytes.

Queue and ring depths recorded only in the Agent 05 outline (not re-audited in this file): deadline ring 8, finalized ring 32, ISR event ring 128, raw ring 256, frame ring 2. Those figures stay **OPEN** candidates.

**Non-adoption rule:** V4 PRECOMP, a fixed 10 Hz-only schedule, ESP-IDF 5.x task shapes, and historical `pcName` strings are evidence of intent, not templates.

### 1.4 Why this is not part of ADR-0002

Issue #23 lock **A** places public task topology and ISR-to-task rules in a new ADR-0003, not inside ADR-0002. Driver pull requests must not invent a second scheduler.

## 2. Requirements and constraints

| ID | Requirement | Source |
|---|---|---|
| R1 | Task topology, priorities, affinity, ISR-to-task rules, bounded waits, and TWDT policy have one public home: this ADR. | #23 A; #25 |
| R2 | FreeRTOS, ESP-IDF, GPTimer, drivers, and TWDT appear only in adapters and `app_main`. `nn2_core` and `nn2_ports` never call `xTaskCreate*`. | ADR-0001; #23 E; G1 naming |
| R3 | Optical frame rate, ON/OFF, \(K\), and absolute deadlines stay in ADR-0002. This ADR does not redefine them. | ADR-0002; #25 |
| R4 | Core-1 hard-RT **ordering** is LED (`nn2_led`) > optical scheduler / DeadlineTimer (`nn2_sched`) > ADS band {`nn2_ads131`, `nn2_ads1299`} (each woken from its ISR). Literal priority numbers stay **OPEN** until Class H. Relative priority between `nn2_ads131` and `nn2_ads1299` stays **OPEN**. | Owner + Agent 05; G1; Owner dual-ADS lock on #25 |
| R5 | V4 22/21/20 and other historical numeric ladders are Class S evidence only (**do-not-lift**). V4’s single ADS priority does not rank the two NN2 ADS tasks. | #25; #26 |
| R6 | Optical frame consumer Proposed placement is Core 0 mid-priority. Affinity stays **OPEN** until Class H. Core 1 below the ADS band {`nn2_ads131`, `nn2_ads1299`} is rejected as the Proposed placement. | Agent 05 Class S; G1 |
| R7 | FreeRTOS `pcName` is `nn2_<role>`, snake_case, ASCII, no spaces, at most 15 characters plus NUL inside default `configMAX_TASK_NAME_LEN` 16. Priority, stack, and affinity macros live in **one** adapter header. | G1; Owner confirm A |
| R8 | LED timing ownership: SlotPlan in `nn2_core` decides when slots fire; the scheduler adapter and DeadlineTimer enforce deadlines; the EmitterControl LED adapter applies pattern, completion, and SafeOff only. | #26 Owner lock |
| R9 | ISR-to-task, bounded-wait, and TWDT rules in §3.8–§3.10 are normative text in the Accepted baseline. On-target proof of those rules stays Class H **NOT RUN**. | Owner outline; Agent 05 §4–§6 |
| R10 | M1 transport is Wi-Fi **STA to a local Visual Studio / ASP.NET (or equivalent) host on the LAN**. Cloudflare tunnel / Workers / edge is a later adapter behind the same ports, not an M1 definition of done. | G1; Owner confirm B |
| R11 | Transport is Wi-Fi only (no BLE). Storage is M2. Power / fuel-gauge is not an M1 priority. | #26 |
| R12 | No clinical claims. No fabricated Class H numbers. A green build does not prove deadlines, WCET, electrical behaviour, or safety. | Standing rules; #25 |
| R13 | Sequencing before a frozen Core-1 numeric ladder: **A(+E) → C → D → B (Class H)**. | #23 |

Non-goals of this ADR: firmware implementation, CMake or `sdkconfig` edits, edits under `reference/**`, flashing, and Class H measurement. Status **Accepted** does not authorize those non-goals.

## 3. Decision

The locks in this section are the **Accepted** topology contract: the approved design baseline for later firmware issues. Accept does not by itself authorize implementation, flash, or Class H. Final affinity, literal priority numbers (including the relative rank of `nn2_ads131` and `nn2_ads1299`), stack depth and unit, and queue or ring depths stay **OPEN** until Class H (§3.5, §3.11).

### 3.1 Layering (ADR-0001)

Dependency direction is unchanged:

```text
Adapters + app_main ──► Ports ──► Core ◄── Application
```

| Layer | May use FreeRTOS / ESP-IDF / GPTimer / TWDT? | Role in this ADR |
|---|---|---|
| `nn2_core` | **No** | Domain types and invariants, including SlotPlan. No tasks, queues, ISRs, or delays. |
| `nn2_ports` | **No** (contracts only) | Timeouts, errors, and ownership in portable terms. No FreeRTOS headers. |
| `nn2_app` | **No** FreeRTOS types | Use-case orchestration. May request start/stop through ports. Never `xTaskCreate*`. |
| Adapters + `app_main` | **Yes** | Sole home of tasks, ISR registration, queues, GPTimer, and TWDT subscription. |

Accepted baseline locks:

1. Tables in this ADR name adapter and composition tasks only. Domain code is never a FreeRTOS task owner.
2. A blocking port documents caller context (task; ISR forbidden unless the port says otherwise), a timeout in portable units (milliseconds), and an error code. Tick conversion stays in the adapter.
3. Optical phase / slot / \(K\) policy stays in ADR-0002 and in core or application. Adapters realize the absolute timebase and the ISR handoff.
4. Directory layout need not mirror layers one-to-one. This ADR does not authorize a tree rewrite.

### 3.2 Relation to ADR-0002

| Concern | Owner | This ADR |
|---|---|---|
| \(f_s \in \{1,2,4,5,8,10\}\) Hz | ADR-0002 | Constraint on frame period and later backlog math. No new rates. |
| Default 50/50 ON/OFF | ADR-0002 | The scheduler task must not redefine duty inside the RTOS table. |
| \(K\) 10/8 and corrected intensity | ADR-0002 | Domain only. |
| Absolute deadlines | ADR-0002 intent | Adapter owns the timer ISR and the task notify. `vTaskDelay` is not the optical clock. |
| STOPPED-only configuration | ADR-0002 | SCAN_STOP versus alarm ownership is co-owned with §3.8 and §3.10. Correctness on target is Class H. |

### 3.3 LED timing ownership

Owner lock from [#26](https://github.com/YerriSuyudHasim/noctinirs-2/issues/26#issuecomment-6211442017), part of the Accepted baseline:

1. **SlotPlan** (`nn2_core`) decides when slots fire.
2. **DeadlineTimer / scheduler adapter** enforces those deadlines.
3. **EmitterControl** (LED adapter) applies the pattern, reports completion, and performs **SafeOff** only. The LED driver does not own a wall-clock schedule.

SafeOff remains a software obligation on the emitter path. This ADR does not waive it and does not claim a measured SafeOff latency.

### 3.4 Task inventory and lifecycle

Every application task is listed here **before** the first `xTaskCreate*` or pinned create in firmware. `nn2_core` never creates tasks, ISRs, or queues. START / STOP / FAULTED ownership is documented per task. SCAN_STOP must not leave an orphan GPTimer alarm (§3.8, §3.10).

| Logical role | `pcName` (Accepted baseline) | Owns | Created by | Lives in |
|---|---|---|---|---|
| Composition | (boot; not an optical task) | Wire ports to adapters; start/stop order | Boot / shutdown | `app_main` only |
| LED / emitter commit | `nn2_led` | Submit emitter updates; SafeOff. No domain policy. | Adapter + composition | Adapter |
| Optical slot scheduler | `nn2_sched` | DeadlineTimer enforcement of SlotPlan. Not phase/\(K\) policy. | Adapter + composition | Adapter |
| ADS131M0x / DRDY sample task | `nn2_ads131` | Sample ingest, stall detect, ring produce on the ADS131M0x path (fNIRS / primary optical sample path as scoped by hardware issues) | Adapter + composition | Adapter |
| ADS1299 / DRDY sample task | `nn2_ads1299` | Sample ingest, stall detect, ring produce on the ADS1299 path. Both ADCs on Nocti board v1 are in product use. | Adapter + composition | Adapter |
| Optical frame consumer | `nn2_frame` | Post-acquisition frame handling toward the sink | Adapter + composition | Adapter |
| Network / transport | `nn2_net` | Soft-RT Wi-Fi command and frame I/O. Must not starve the Core-1 chain. | Adapter + composition | Adapter (Core 0 candidate) |
| Health / TWDT helper | `nn2_health` **only if scoped** | Subscription reset; fault-taxonomy hooks | Adapter | Adapter; not an M1 requirement until scoped |
| Dark / diagnostic | name only if scoped | Diagnostics below the network band | Adapter | Not an M1 requirement in this ADR |

The sole placeholder task name `nn2_ads` is **superseded** (Owner lock on [#25](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226293188)). Do not create a third task with that name.

Architect Class S planning note ([#25](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226298179)): one portable ADC port shape, `nn2_port_adc_t`, and two adapters. Chip product names stay in adapter components and in these `pcName` strings. They do not enter `nn2_core` type names. This note records that planning shape only. It does not add a port API and does not implement adapters.

Dual-rate acquisition and timestamp alignment are follow-up [#31](https://github.com/YerriSuyudHasim/noctinirs-2/issues/31). M1 stays fNIRS-first. Dual-rate is not an M1 blocker and is not specified by this ADR.

M1 focus is the optical chain plus the Wi-Fi command and PC/server path (`nn2_net`). Dark diagnostic and `nn2_health` stay out of M1 until a later scope decision. Power and storage are out of M1 (§3.12).

### 3.5 Priority ordering, affinity, stacks, and queues

**Ordering lock (Accepted baseline; literal numbers OPEN):** on the Core-1 hard-RT optical chain the priority order is:

**LED (`nn2_led`) > optical scheduler / DeadlineTimer (`nn2_sched`) > ADS band {`nn2_ads131`, `nn2_ads1299`}, each woken from its ISR.**

Relative priority **between** `nn2_ads131` and `nn2_ads1299` stays **OPEN**. This ADR does not rank them. Both remain Core-1 candidates. Wi-Fi (`nn2_net`) stays a Core 0 candidate; dual ADS does not move Wi-Fi onto Core 1. Final affinity pins stay **OPEN** until Class H (Agent 05 Class S on [#25](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226299155)).

Rationale (Agent 05, Owner-delegated, dual-ADS Class S): LED edge completion must not be delayed by scheduler or ADS work; the scheduler must outrank the ADS band so deadline ticks preempt sample processing; both ADS tasks are scheduled sample paths below the scheduler.

**Literal priority numbers are OPEN until Class H.** This ADR assigns none. V4 22/21/20 are do-not-lift evidence (§1.3). The historical ADS priority 20 does not assign an NN2 number to either task and does not rank `nn2_ads131` against `nn2_ads1299`.

**Rev 3 scoped deviation (2026-10-29).** The V4 IDF 6.1 image under `firmware/v4-idf61` runs one ADS131M04 task above the LED task. That order, its boot82 rationale, and the conditions that keep it in force are §3.14. It does not replace the Accepted dual-ADS baseline in the paragraph above.

**Frame consumer affinity (Proposed):** Core **0**, mid-priority. **Reject** placing `nn2_frame` on Core 1 below the ADS band {`nn2_ads131`, `nn2_ads1299`} as the Proposed placement. Core 1 stays the hard-RT optical chain (LED, DeadlineTimer scheduler, and the ADS band). The consumer is post-acquisition work; another runnable on Core 1 risks extra jitter on that chain. **Affinity remains OPEN until Class H** measures it. The Proposed placement is not a final pin.

**Risk carried with that placement:** M1 transport is Wi-Fi on Core 0. `nn2_frame` must not starve under Wi-Fi softirq load. Runnable latency and TWDT margin with Wi-Fi active are Class H bring-up measurements (**NOT RUN**).

| Task / ISR | Core (Proposed) | Priority | Stack | Queue / ring | Notes |
|---|---|---|---|---|---|
| `nn2_led` | Candidate **Core 1** (OPEN until Class H) | **OPEN**. Order: above `nn2_sched` and the ADS band. V4 evidence 22, do-not-lift. | **OPEN** | Submit queue depth **OPEN** | Highest hard-RT candidate only after Class H WCET versus DRDY |
| `nn2_sched` | Candidate **Core 1** (OPEN) | **OPEN**. Order: below LED, above the ADS band. V4 evidence 21, do-not-lift. | **OPEN** | Deadline ring **OPEN** | Absolute timebase in the adapter. No `vTaskDelay` as the optical clock. |
| `nn2_ads131` | Candidate **Core 1** (OPEN) | **OPEN**. Order: below scheduler, inside the ADS band. Relative order versus `nn2_ads1299` **OPEN**. V4 evidence 20, do-not-lift; it does not rank the two ADS tasks. | **OPEN** | ISR event ring and raw ring **OPEN** | Must not be starved by LED/I2C on the same core |
| `nn2_ads1299` | Candidate **Core 1** (OPEN) | **OPEN**. Order: below scheduler, inside the ADS band. Relative order versus `nn2_ads131` **OPEN**. V4 evidence 20, do-not-lift; it does not rank the two ADS tasks. | **OPEN** | ISR event ring and raw ring **OPEN** | Must not be starved by LED/I2C on the same core |
| `nn2_frame` | **Core 0 mid-prio Proposed**; affinity **OPEN** until Class H | **OPEN**. V4 evidence priority 5, do-not-copy. | **OPEN** | Frame ring **OPEN**; sink **nonblocking** | Core 1 below the ADS band rejected as Proposed placement |
| `nn2_net` | Candidate **Core 0** | Candidate soft band **OPEN** (V4 evidence 4, do-not-lift) | **OPEN** | TX/RX depths **OPEN** | Must not join the Core-1 hard ladder without a later justification |
| Dark / diagnostic, if scoped | Candidate **Core 0** | Below the network band **OPEN** | **OPEN** | n/a | V4 priority 12 / core 0 is evidence only. Not M1. |
| DRDY / timer ISRs | N/A (ISR) | N/A | N/A | Notify or queue-from-ISR only | §3.8. No heavy work in the ISR. |

Stack unit (words versus bytes) is **OPEN**. Do not copy V4 `*_STACK_BYTES` 6144.

Class U may later validate that a config table has a single source and a consistent order. Class U cannot lock the numbers. Class H gates any claim that the ladder meets optical deadlines.

### 3.6 Task naming

G1 unanimous, Owner-confirmed (Accepted baseline; not implemented):

| Rule | Lock |
|---|---|
| FreeRTOS `pcName` | `nn2_<role>`, snake_case, ASCII, no spaces |
| Length | At most **15** characters plus NUL, inside the default `configMAX_TASK_NAME_LEN` of 16 |
| Examples | `nn2_led`, `nn2_sched`, `nn2_ads131`, `nn2_ads1299`, `nn2_frame`, `nn2_net`. `nn2_health` only if a health task is scoped. `nn2_ads131` is 10 characters and `nn2_ads1299` is 11; both fit 15 plus NUL. The placeholder `nn2_ads` is superseded and is not an extra task. |
| Macros | Priority, stack, and affinity macros in **one** adapter header only (illustrative name `nn2_rtos_config.h` or equivalent). Not in `nn2_core`, port headers, or scattered driver files. |
| Inner layers | `nn2_core` and `nn2_ports` never call `xTaskCreate*` |

Logical titles in this ADR stay human-readable and map 1:1 onto those `pcName` strings.

Agent 03 implementation notes only, and not a firmware change in this pull request:

- Expose each name as a `static const char` or a macro beside the create site so `vTaskList` and log filters stay stable.
- A later stack-watermark check should key off the same `nn2_*` strings. Keep the set short.

Truncated or opaque names hurt TWDT and `uxTaskGetSystemState` diagnostics during Class H bring-up. No shorter aliases are required for the examples above.

### 3.7 M1 transport

**Accepted baseline M1 path:** the device joins Wi-Fi as a **station** and talks to a **local Visual Studio / ASP.NET (or equivalent) process on the LAN**.

**Deferred:** Cloudflare tunnel, Workers, or other edge front ends. They are a **later adapter** behind the same ports:

- **FrameSink / Transport**
- pure **WireCodec**
- **CommandSource**

Cloudflare is not an M1 definition of done. The network adapter must not depend on optical or domain types. The composition root injects the endpoint. Prefer a lighter Core-0 Wi-Fi load until Class H has measured optical deadlines (**NOT RUN**).

BLE is out of scope (§3.12).

### 3.8 ISR → task handoff (normative; Accepted baseline)

Agent 05 outline §4, adopted as normative Accepted-baseline text:

1. **ISR allowed work:** clear the interrupt, timestamp or capture minimal state, and wake a task with `xTaskNotifyGive` / `vTaskNotifyGiveFromISR` or a bounded `xQueueSendFromISR` that has a documented overflow counter. **No** I2C, allocation, logging, or domain policy in the ISR.
2. **Deferred work** always runs in a named task from §3.4–§3.5. ADS deferred work runs in `nn2_ads131` or `nn2_ads1299`, not in a task named `nn2_ads`.
3. **GPTimer / alarm:** arm, disarm, and the `scan_active` flag (or successor) have a documented owner. SCAN_STOP from any core must not leave an orphan alarm. V4 comments on this race are evidence of the hazard class, not a patch to copy.
4. **Critical sections:** ISR and task critical-section length budgets are stated beside the implementation. A claim that a critical section is safe versus DRDY spacing is a **Class H** claim. None is made here.
5. These public rules live in this ADR, not as one-off exceptions inside driver pull requests.

### 3.9 Queues, timeouts, and bounded waits (normative; Accepted baseline)

Agent 05 outline §5, adopted as normative Accepted-baseline text. Wait contracts are finalized under sequence **C**, after this skeleton (**A**).

1. **No `portMAX_DELAY`** on SCAN RUNNING, fault-detection, or recovery waits unless this ADR lists an exception **and** an alternate watchdog covers that wait.
2. Idle or STOPPED waits may be unbounded **only if** TWDT (or an explicit exclusion in §3.10) covers the task.
3. Every blocking port API documents caller context, a timeout (portable milliseconds at the port; ticks only inside the adapter), and the error taxonomy. The optical frame sink stays **nonblocking**.
4. Overflow policy is **count plus a fault or quality flag**. Required samples or events are never dropped silently.
5. Ring depths, when later chosen, are justified by a backlog model (DRDY rate times the maximum preemption window) written beside the table. That model is not a Class H measurement.

### 3.10 TWDT coverage (normative; Accepted baseline)

NN2 TWDT is **new**. It is not a lift from V4 (`esp_task_wdt` has 0 hits under `reference/v4` at the input commit). Mechanisms land under sequence **D**, after **A** and **C**, and they depend on **B** for critical-section and I2C WCET budgets. Those budgets are **NOT RUN**.

1. Every hard-RT task in §3.5 documents either **TWDT subscribed** or an **explicit exclusion plus an alternate** (software stall timer, interrupt watchdog story, or equivalent).
2. Kick points are named (task-loop iteration or a known safe idle). A missed kick has a defined recovery hook. The hook’s behaviour is later Class B/U evidence, not claimed here.
3. Ring overflow increments a counted fault (§3.9).
4. The SCAN_STOP versus alarm race protocol is co-owned with Firmware. On-target correctness is Class **H** and is **NOT RUN**.

### 3.11 What stays open until Class H

| Topic | State in this ADR |
|---|---|
| Final core affinity, including the frame consumer | Proposed Core 0 mid-prio for `nn2_frame`; **OPEN** until Class H |
| Literal priority numbers | **OPEN**. Ordering LED (`nn2_led`) > sched (`nn2_sched`) > ADS band {`nn2_ads131`, `nn2_ads1299`} is the Accepted baseline. The V4 IDF 6.1 image order in §3.14 is a scoped deviation from that baseline. |
| Relative priority of `nn2_ads131` vs `nn2_ads1299` | **OPEN** until Class H. Not ranked by this amend. |
| Stack depth and unit | **OPEN** |
| Queue and ring depths | **OPEN** |
| Timing budgets, WCET, jitter, DRDY spacing versus I2C | **OPEN**. Class H **NOT RUN**. |
| TWDT behaviour under Wi-Fi and optical load | Policy is the Accepted baseline; measurement **NOT RUN** |

### 3.12 Milestone scope (from #26)

| Topic | Decision |
|---|---|
| Power / fuel-gauge (BQ24295, MAX17048) | **Not an M1 priority.** No PowerPort work in M1. |
| Storage | **Deferred to M2.** |
| Transport | **Wi-Fi only.** No BLE in current scope. |
| LED timing split | SlotPlan → DeadlineTimer / scheduler → EmitterControl SafeOff (§3.3). |

### 3.13 Non-goals

- No firmware, CMake, `sdkconfig`, or `reference/**` change in the ADR pull request.
- No flash, no Class H session, no claim that a green CI build proves hardware timing.
- Status **Accepted** approves this topology as the design baseline only. It does not authorize implementation, flash, or Class H. This dual-ADS amend still needs a fresh Agent 07 independent review on its head SHA before Owner merge.
- No optical geometry restatement (ADR-0002).
- No clinical, diagnostic, or patient-safety claims.
- No BLE, no M1 storage, no M1 power/fuel-gauge.

### 3.14 Rev 3 — V4 IDF 6.1 Core-1 order (2026-10-29)

This section amends the Accepted ordering in §3.5 and R4 for one image. Status stays **Accepted**. The amendment does not authorize flash, Class H, or a merge.

**Implemented order**, single source `firmware/v4-idf61/components/nocti_v4_common/include/nocti_v4_rtos.h`, core 1:

`nocti_v4_ads` priority 23 > `nocti_v4_led` priority 22 > `nocti_v4_sched` priority 15.

**Rationale (boot82).** The previous image used the historical ladder ADS 20, scheduler 21, LED 22, all on core 1. After rate qualification (1024 samples, 8000 SPS, period 124–125 µs) the first LED ON completed, then `EVENT_BACKLOG` at `PROCESS_EVENT`. Depth max was 4, `adc_missed_backlog` was 3, SPI was 32–45 µs (average 33), and CRC, SPI, and status errors were 0. ADS131M04 keeps only the latest conversion, so the three older queued DRDY events were overwritten while the LED task and the scheduler outranked the ADS task. Putting ADS above LED, and the scheduler below both, is the response to that evidence. The design record is `docs/V4_IDF61_RTOS_HANDOFF.md`.

**Conditions under which this order stands:**

1. It applies only to `firmware/v4-idf61`, which has one ADS131M04 sample task and no `nn2_ads1299` task.
2. The Accepted product baseline remains LED (`nn2_led`) > scheduler (`nn2_sched`) > ADS band {`nn2_ads131`, `nn2_ads1299`}. Relative rank of the two ADS tasks stays **OPEN**. This revision does not assign Class H numbers to those `nn2_*` names.
3. The order stands until a Class H measurement of DRDY service versus LED I2C completion, on a named board and image hash, records that ADS priority 23 makes the LED miss its slot edge, or until the Owner writes a different order. Boot82 itself stays open until an on-target `health` report shows `adc_missed=0` and `adc_backlog_catchup=0`.
4. Literal values 23, 22, and 15 are the V4 table. They are not a freeze of every future priority number. Wi-Fi stays on core 0.

## 4. Evidence classes

| Class | What it can prove | What it does not prove |
|---|---|---|
| **S** (static) | The table exists; include and boundary review; path citations | Scheduling, WCET, electrical safety |
| **B** (build) | Firmware builds; one priority source; no duplicate magic numbers | Deadlines under load |
| **U** (host/unit) | Config-table checks; fault-injected timeouts and overflows in simulation; STOPPED lifecycle logic without a real alarm | On-target jitter, TWDT, Wi-Fi coexistence |
| **H** (on-target) | Distributions tied to a SHA, board revision, clocks, sample count, and p50/p99 | Clinical validity |

**Class H gates** (from #23 / Agent 05): locking Core-1 priority numbers; LED I2C WCET versus DRDY spacing; TWDT and SCAN_STOP race claims under real load, including Wi-Fi. Energized optical work still needs a separate Owner authorization and the ADR-0002 bring-up current band. Until then every such claim is **NOT RUN**. V4 microsecond figures are not NN2 verified timings.

A successful firmware build is Class **B** only. It is not Class H, not unit evidence, and not proof of timing, electrical behaviour, safety, or clinical validity.

## 5. Alternatives considered

| Alternative | Description | Pros | Cons | Outcome |
|---|---|---|---|---|
| A. Lift V4 22/21/20 as NN2 constants | Copy the historical Core-1 ladder verbatim. | Familiar; already ordered LED > sched > ADS. | Freezes unmeasured numbers; violates do-not-lift. One historical ADS priority cannot rank `nn2_ads131` against `nn2_ads1299`. | **Rejected** as constants. Ordering kept as LED > sched > ADS band; numbers and the relative ADS ranking stay **OPEN**. |
| B. Frame consumer on Core 1 below the ADS band | Keep post-processing on the hard-RT core at a lower priority. | Short handoff to the consumer. | Extra runnable on the critical core; inversion and jitter risk under Wi-Fi/ISR load. | **Rejected** as the Proposed placement. |
| C. `vTaskDelay` as the optical clock | Slot timing by relative delays. | Simple. | Drifts; contradicts ADR-0002 absolute deadlines. | **Rejected**. |
| D. Domain or ports call `xTaskCreate*` | Tasks created inside `nn2_core` / `nn2_ports`. | Fewer adapter files. | Breaks ADR-0001; blocks host tests. | **Rejected**. |
| E. Cloudflare tunnel as M1 definition of done | IoT path via tunnel / Workers / edge for first packets. | Remote demo. | Extra hops, auth, and deploy before optical bring-up; variable latency hides adapter bugs. | **Deferred** as a later adapter behind the same ports. |
| F. BLE alongside Wi-Fi in M1 | Second transport now. | Phone bring-up. | Out of #26 scope; more Core-0 load before Class H. | **Rejected** for current scope. |
| G. Scatter priority macros per driver | Each adapter header picks its own numbers. | Local edits. | Duplicate magic numbers; review cannot see one ladder. | **Rejected**. One adapter header. |

## 6. Consequences

**Positive**

- One public place for topology and ISR rules, so driver pull requests cannot invent a second ladder.
- Core-1 ordering is decided without pretending the numbers are measured.
- Both on-board ADCs have distinct `pcName` strings inside the ADS band, and the ranking between those two tasks stays unfrozen.
- Frame-consumer placement is explicit and reversible after Class H.
- Short `nn2_*` names stay inside the default task-name buffer and remain visible to TWDT diagnostics.
- M1 can bring up command and frame flow on a LAN host without a tunnel account.
- LED schedule ownership matches SlotPlan → DeadlineTimer → SafeOff.

**Negative**

- Affinity and every numeric cell stay open, so implementation cannot freeze a ladder in the first firmware change.
- Two Core-1 ADS candidates increase contention below the scheduler. Relative priority, stacks, and queues stay unmeasured.
- Core 0 hosts both Wi-Fi and the frame consumer; coexistence is an unmeasured risk.
- TWDT is new work, not a historical copy.
- Ports must stay free of FreeRTOS types, so adapters own every tick conversion.

**Neutral**

- ADR-0001 remains Proposed. A later Accept of ADR-0001 only needs a cross-link check, not a change to optical geometry.
- Sequence **C**, **D**, and Class **H** (**B**) are later issues. This ADR does not close them.

## 7. Affected components and interfaces (future)

No component is added or edited by this ADR. Later implementation, under its own issues, is expected to touch only:

- One RTOS adapter header for `pcName`, priority, stack, and affinity macros.
- Adapter tasks `nn2_led`, `nn2_sched`, `nn2_ads131`, `nn2_ads1299`, `nn2_frame`, `nn2_net`. The superseded placeholder `nn2_ads` is not a third task.
- Ports already anticipated by ADR-0001: emitter, timer, adc, stream/frame sink, command. Architect Class S on #25 plans one ADC port shape (`nn2_port_adc_t`) and two adapters. WireCodec stays pure. This ADR does not define that port’s operations.
- `app_main` as the composition root that creates tasks.

`reference/**`, `CMakeLists.txt`, `sdkconfig`, and active firmware sources are unchanged by the pull request that adds this file.

## 8. Verification strategy

| Class | What | When |
|---|---|---|
| **S** | This file exists and Status is **Accepted**; task table lists `nn2_ads131` and `nn2_ads1299`; sole `nn2_ads` appears only as the superseded placeholder; ordering LED > sched > ADS band and do-not-lift stated; relative ADS priority and other OPEN cells remain OPEN; no FreeRTOS includes under `nn2_core` / `nn2_ports` once those trees exist | This pull request (document review). Include grep is **NOT APPLICABLE** until inner layers exist, and a skip is not a pass. |
| **B** | Single macro header; build with stubs; no duplicate priority literals | Later firmware issue. Not this pull request. |
| **U** | Host checks for overflow counters, timeout errors, and STOPPED lifecycle logic | Later. Not hardware. |
| **H** | On-target p50/p99, TWDT, DRDY versus I2C WCET, Wi-Fi versus `nn2_frame` latency | **NOT RUN.** Separate Owner authorization before energize. |
| **R** | Prior Agent 07 APPROVE covered Proposed PR #27 head `e8dfbe852e1d920fce476a90ce3a495f4b6ed33c` and Accept PR #29 head `cbb0c9b36a3825a9bd8c1bbd756c32e6e41c6059`. This dual-ADS amend still needs a fresh Agent 07 independent review on its head SHA before Owner merge. Author ≠ reviewer. | Process. Not yet satisfied for this revision. |

This pull request’s evidence class is **static / document review** only. CI that compiles the scaffold, if it runs, is Class **B** for the unchanged scaffold. It does **not** prove this topology.

## 9. Open questions and risks

| # | Question / risk | Status (2026-10-17) | Owner of answer |
|---|---|---|---|
| Q-1 | Final affinity for `nn2_frame` and the rest of the table | Proposed Core 0 mid-prio; **OPEN** until Class H. Core 1 below the ADS band rejected as Proposed placement. Both ADS tasks are Core-1 candidates; pins stay **OPEN**. Wi-Fi stays a Core 0 candidate. | RTOS + Class H; Owner Accept |
| Q-2 | Literal priorities, stacks, queue depths | **OPEN**. Ordering LED > sched > {`nn2_ads131`, `nn2_ads1299`} is the Accepted baseline. | RTOS; Class H before lock |
| Q-3 | Stack unit (bytes vs words) given V4 `*_STACK_BYTES` 6144 vs the outline’s “words” column | **OPEN**. Do not lift 6144. | Firmware + RTOS |
| Q-4 | Whether `nn2_health` or a dark-diagnostic task exists in M1 | Not in M1 unless later scoped | Owner |
| Q-5 | Wi-Fi softirq versus Core-0 frame consumer | Called out; measurement **NOT RUN** | RTOS; Class H |
| Q-6 | SCAN_STOP versus GPTimer orphan alarms | Protocol required (§3.8, §3.10); on-target proof Class H **NOT RUN** | Firmware + RTOS |
| Q-7 | Critical-section and I2C WCET versus DRDY spacing | Class H gate **B**; **NOT RUN**. Needs separate Owner authorization and the bring-up current band. | RTOS + Hardware |
| Q-8 | ADR-0001 still Proposed | Cross-link only if ADR-0001 is later Accepted | Architect |
| Q-9 | Relative priority between `nn2_ads131` and `nn2_ads1299` | **OPEN**. Not frozen by this amend. | RTOS + Class H |
| Q-10 | Dual-rate acquisition and timestamp alignment for the two ADCs | Follow-up [#31](https://github.com/YerriSuyudHasim/noctinirs-2/issues/31). M1 is fNIRS-first. Dual-rate is not an M1 blocker and is not specified here. | Later issue; not this ADR |

## 10. References

- Issue [#25](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25) — this ADR.
- Issue [#31](https://github.com/YerriSuyudHasim/noctinirs-2/issues/31) — follow-up dual-ADC / dual-rate and timestamp alignment. M1 stays fNIRS-first. Not an M1 blocker. Not implemented by this amend.
- Issue [#26](https://github.com/YerriSuyudHasim/noctinirs-2/issues/26) — scope locks (power, storage, Wi-Fi only, LED split).
- Issue [#23](https://github.com/YerriSuyudHasim/noctinirs-2/issues/23) — sequencing A(+E) → C → D → B (Class H).
- Issue [#24](https://github.com/YerriSuyudHasim/noctinirs-2/pull/24) — ADR-0002 Accepted; squash `791e35b`.
- ADR-0001: `docs/adr/0001-clean-architecture-boundaries.md` (Proposed at `791e35b`).
- ADR-0002: `docs/adr/0002-fnirs-optical-acquisition-timing.md` (Accepted at `791e35b`).
- Base of the Proposed text: `main` @ `791e35b79ba8cef343280d76973fee21d3b41e42`.
- Proposed land (squash of PR #27): `27e33d874387579986d781d547c61d8441d3631b`.
- Accepted base of this amend: `main` @ `6309c06bdedd01bd2430ccd62b5c4604b34f9d8a` (squash of PR #29).

Issue comments folded into this revision:

1. Agent 05 RTOS outline: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6210637276
2. Agent 02 Architect skeleton: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6210656497
3. Owner outline answers: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6212585197
4. Agent 03 Firmware A+B: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6212597635
5. Agent 02 Architect A+B: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6212600464
6. Agent 05 Class S decisions: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6212603914
7. Orchestrator G1 consensus: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6212631613
8. Owner write authorization: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6212642900
9. #26 scope locks: https://github.com/YerriSuyudHasim/noctinirs-2/issues/26#issuecomment-6211442017
10. Owner dual-ADS lock (via Nori, 2026-10-17): https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226293188
11. Architect Class S (one `nn2_port_adc_t`, two adapters): https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226298179
12. Agent 05 RTOS Class S (ordering set; relative ADS priority OPEN; both Core-1 candidates; Wi-Fi Core 0): https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226299155
13. Orchestrator G1 fold: https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226304294
14. Owner write authorization for this amend (via Nori, 2026-10-17): https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226379710
15. Follow-up dual-rate issue (not implemented here): https://github.com/YerriSuyudHasim/noctinirs-2/issues/31

Evidence only (not requirements), priorities re-checked at `791e35b`:

- `reference/v4/components/nocti_v4_async_led/nocti_v4_async_led.c`
- `reference/v4/components/nocti_v4_scheduler/include/nocti_v4_scheduler_config.h`
- `reference/v4/components/nocti_v4_common/include/nocti_v4_ads_config.h`
- `reference/v4/components/nocti_v4_network/include/nocti_v4_network.h`
- `reference/v4/components/nocti_v4_optical/nocti_v4_optical.c`

Standing rules: no merge, release, or flash without specific Owner approval; `reference/**` read-only.

## 11. Revision history

| Rev | Date | Change | Status |
|---|---|---|---|
| 0 | 2026-10-16 | Initial Proposed record from #25 Owner-confirmed G1 consensus and Agent 05 §4–§6. Core-1 ordering LED > sched > ADS with numbers OPEN. Frame consumer Core 0 mid-prio Proposed, affinity OPEN. Naming `nn2_*`. M1 LAN STA transport. Class H **NOT RUN**. No firmware. | **Proposed** |
| 1 | 2026-10-17 | Status **Proposed → Accepted** under Owner Accept write authorization `confirm_accept_write` (2026-10-17 Asia/Shanghai). Prior Proposed land SHA `27e33d874387579986d781d547c61d8441d3631b` (squash of PR #27). Agent 07 IR APPROVE on Accept PR #29 head `cbb0c9b36a3825a9bd8c1bbd756c32e6e41c6059`; squash land `6309c06bdedd01bd2430ccd62b5c4604b34f9d8a`. Topology contract is the approved design baseline for later firmware issues. Does not authorize implementation, flash, or Class H. Affinity, literal priorities, stacks, and queues stay **OPEN**. Class H / flash / WCET **NOT RUN**. No firmware. Inventory at this rev still used the sole placeholder `nn2_ads`, superseded in rev 2. | **Accepted** |
| 2 | 2026-10-17 | Amend of Accepted content (Status unchanged). Owner dual-ADS lock via Nori on [#25](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226293188): both ADCs on Nocti board v1; sole placeholder `nn2_ads` superseded by `nn2_ads131` and `nn2_ads1299`. G1 Class S: Architect one `nn2_port_adc_t` / two adapters ([6226298179](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226298179)); RTOS ordering `nn2_led` > `nn2_sched` > {`nn2_ads131`, `nn2_ads1299`}, relative ADS priority **OPEN**, both Core-1 candidates, Wi-Fi Core 0 ([6226299155](https://github.com/YerriSuyudHasim/noctinirs-2/issues/25#issuecomment-6226299155)). Affinity, literal priorities, stacks, and queues stay **OPEN**. Dual-rate follow-up [#31](https://github.com/YerriSuyudHasim/noctinirs-2/issues/31) is not implemented here (M1 fNIRS-first; not an M1 blocker). Class H / flash / firmware **NOT RUN**. Fresh Agent 07 IR required on this head before Owner merge. | **Accepted** |
| 3 | 2026-10-29 | Scoped deviation for `firmware/v4-idf61` only (§3.14): `nocti_v4_ads` 23 > `nocti_v4_led` 22 > `nocti_v4_sched` 15 on core 1. Rationale is boot82 (ADS 20 under scheduler 21 and LED 22 left three DRDY conversions overwritten). Stands until Class H shows the LED missing its edge, or the Owner writes another order. The Accepted dual-ADS baseline is unchanged. Class H **NOT RUN**. | **Accepted** |
