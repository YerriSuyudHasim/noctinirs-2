# ADR-0001: Clean Architecture Layer Boundaries for NoctiNIRS 2

| Field | Value |
|---|---|
| ID | ADR-0001 |
| Title | Clean architecture layer boundaries (Core / Application / Ports / Adapters) |
| Status | **Accepted** (bounded static architecture framework; not product implementation approval) |
| Date | 2026-10-21 (A2 Owner clarification record, rev 5; original bounded acceptance 2026-10-18) |
| Issue | #3 (M0.3 — Clean Architecture ADR) |
| Author role | Architect (draft) |
| Independent reviewer | Group 2 Independent Technical Reviewer; separately attributed reports linked in Section 0 (not the implementation author) |
| Approver / merge | Project Owner `YerriSuyudHasim` only |
| Decision authority | OWNER-M0-A1-001 (recorded in `project.yaml` `governance.decision_ref`) |
| Input commit | `noctinirs-2` `main` @ `52d9f85d6ce454b7e1ef97cc7908bbae92b49d4f` (merge of PR #5) |
| Governance input | `ai-engineering-hub` `main` @ `e645829a726b1008c46373d7ef2b25929988d7f1` (read 2026-10-16) |
| Evidence class of this document | Static (design review only) |

> This ADR describes an accepted *target* architecture, not a verified product implementation. The input-commit scaffold observations below are historical. Acceptance of the static framework does not establish runtime, hardware, timing, safety or clinical conformance.

## 0. Owner decision record (bounded acceptance)

**Decision: ACCEPT. Decision date: 2026-10-18 at 09:22:14 UTC.** Human Owner
`YerriSuyudHasim` accepted the bounded static architecture framework in the
[actual Owner decision](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6248188579),
referring to the [decision packet](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6248048761).
The accepted proposal is identified by complete Git tree
`6fb344225d6ea7a489e7eaf5b18d0b5bebf3b88c`, reviewed at
`3c24189d455cefc52143134f57cdff31528256ef` and preserved by actual #43 squash
`d92796d73806e2e97092329be3741b3ddac19c54`.

The [later final integration authorization](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6256494704)
authorized only that integration and explicitly left ADR source recording separate.
The [execution record](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6256587588)
confirms exact tree preservation and post-merge CI. This revision records the existing
decision through a separate reviewable change; it does not backdate the source edit,
alter historical Proposed revisions, or grant merge authority to this revision.

The decision's technical inputs are the [non-author integration approval](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6247562819)
and [bounded Firmware/RTOS consultation](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6247910114).
The later [non-author reconciliation confirmation](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6256423199)
verifies preservation of that accepted tree; it is not an input to the earlier decision.
These are separately attributed Owner-account relay reports, not native GitHub APPROVE
reviews. One assigned non-author assessor held the reviewer and both consulted roles;
they are not three independent people. Consultation recommended acceptance with no
current static-design amendment, not approval of future concurrency or hardware code.

**Accepted scope:** the Core/Application/Ports/Adapters contract, Q-10 strict C11
enforcement design, Q-11 complementary static/compiler/manual strategy and disclosed
residual risks, canonical N6 policy ownership, and port-interface contract template.
The full N6 migration is merged; #39 was only its partial prerequisite. F1 remains
**CLOSED** for the corrected independent per-header probes. **R1/R2 retain only their
existing conditionally accepted scope**; this record accepts no new defect or broader
exception. See the linked reports for their exact conditions.

**Retained obligations:** Sections 3.6-3.10 and 9 remain binding. Macro/conditional,
transitive/generated-header, header-shadow, root/toolchain, link/callback and
standard-header limits remain explicit manual/deferred obligations. Actual product
layers require isolated C11 host builds of all inner translation units/headers,
retained full dependencies and compile commands, configuration coverage and behavior
tests. Each atomic use needs its own RTOS/target review; implementation-specific
tasks/ISRs, synchronization, assertion/watchdog behavior, timing and hardware parameters
are not approved here. Production Class S is **NOT APPLICABLE (not evidence)** while
layers are absent; tooling fixtures and Class B do not substitute for product Class U
or physical evidence. Accepted residual limits are not unfinished Issue #11 strategy
criteria; future product evidence has not been supplied by accepting them.

**N5 restoration:** restore meaningful real `class-u-host-tests`, bound to GitHub
Actions integration **15368**, before the first production `nn2_*` component/runtime
merge (including #13), or **2026-10-25T01:00:00Z**, whichever is earlier. Restore at
expiry absent a new explicit Owner-approved extension. Synthetic/empty/skipped success
is insufficient. [Approved exception](https://github.com/YerriSuyudHasim/ai-engineering-hub/issues/2#issuecomment-6236882761).
No issue is closed by this record. See the [Issue #11 matrix and N5 sequence](../M1_ISSUE11_ACCEPTANCE_RECORD.md)
for review/closeout boundaries and the separate #13/#15/#40 work.

## 1. Context

### 1.1 Problem

NoctiNIRS 2 is a new ESP32-S3 firmware product that must follow clean architecture: dependencies point toward core policy, never toward platform details (product `AGENTS.md`, "Active firmware baseline"). No document yet defines the layers, their responsibilities, the allowed dependency edges, or how those rules are verified. Without that contract, implementation would likely drift toward the historical component-per-peripheral structure, with ESP-IDF/FreeRTOS types inside policy code, which prevents host testing and makes review difficult.

### 1.2 Current scaffold state (verified at `52d9f85`)

- `CMakeLists.txt`: standard ESP-IDF project, `PROJECT_VER "0.1.0"`.
- `main/CMakeLists.txt`: registers only `app_main.c`.
- `main/app_main.c`: includes only `esp_log.h`; logs the version and a warning that no sensor, LED, wireless, or clinical behavior is implemented.
- `sdkconfig.defaults`: debug optimization and INFO log level only.
- No `components/` directory, no tests, no build CI. The only workflow is `.github/workflows/reference-integrity.yml`.
- `README.md` states that the ESP-IDF build and on-target testing have not been run.

**Gap:** none of the four layers exists. The scaffold is a composition root (adapter zone) with no inner layers, so it trivially violates no rule. It does **not** demonstrate the architecture.

### 1.3 Historical references (evidence only)

`reference/v2`, `reference/v3`, and `reference/v4` are read-only historical snapshots (`project.yaml` `historical_references`, `agent_edits: forbidden`, `active_build_inputs: false`). From a static skim at `b1c48083c90368aed126b447eea3a89a29ecad73` (unchanged under `reference/` by PR #5 per its scope):

- All three use **component-per-subsystem** decomposition (e.g. `reference/v3/components/nocti_v3_{adc,board,common,health,led,optical,stream,timebase,wifi}`, `reference/v4/components/nocti_v4_{ads,async_led,board,common,health,i2c,led,network,optical,scheduler,time}`), with ESP-IDF and FreeRTOS used directly inside subsystem logic and in large `main/app_main*.c` files.
- `reference/v3/docs/V3_ARCHITECTURE_CONTRACT.md` and `reference/v3/docs/V3_P5_ARCHITECTURE.md` document useful *ideas*: explicit object ownership tables, ISR-minimal callbacks, static SPSC rings, no heap after START, nonblocking network backpressure, and session/config epochs. V3 itself reports target build and hardware validation as **not verified** (`reference/v3/README.md`).
- Only V3 has a host-test seam (`reference/v3/tools/host_stubs`, `reference/v3/tools/test_stream_compat.c`).

**Non-adoption rule:** historical structure, parameters (rates, slot timing, pins, bus speeds, emitter currents), protocols, and task/core plans are **not** NoctiNIRS 2 requirements. No historical tree is copied wholesale. Any reuse of a historical idea or code fragment requires its own issue or ADR with a traceable justification and review (see Issue #4 for the assessment plan).

## 2. Requirements and constraints

Owner decisions (OWNER-M0-A1-001):

| ID | Constraint |
|---|---|
| C-1 | Human owner `YerriSuyudHasim`; backup owner unassigned; merge only by Owner. |
| C-2 | Risk class `non-clinical-research-prototype`; no clinical, diagnostic, or patient-safety claims. |
| C-3 | Language: **C11**. Inner layers (Core, Application, Ports) compile with `-std=c11` (see 3.4). |
| C-4 | Layer naming: **Core / Application / Ports / Adapters** (Owner decision OWNER-M0-A1-001 r1.1). |
| C-5 | Target ESP32-S3; ESP-IDF 6.1 line, exact version pending verification in Issue #2. |
| C-6 | Baseline version 0.1.0. |

Architectural requirements:

| ID | Requirement |
|---|---|
| R-1 | Core contains no dependency on ESP-IDF, FreeRTOS, drivers, transport, storage, or device-specific code. |
| R-2 | All source dependencies point inward toward Core. |
| R-3 | Core, Application, and Ports compile and run on a host toolchain with fake ports. |
| R-4 | RTOS primitives (tasks, queues, semaphores, ISRs) are Adapter concerns. |
| R-5 | Hardware parameters (pins, clocks, bus speeds, electrical/optical limits) are defined only in approved Adapter/board configuration, never in Core. |
| R-6 | Observability (status codes, fault identities, health counters) is defined inward and exported through Ports. |
| R-7 | Historical behavior is adopted only through a separate issue/ADR. |
| R-8 | Layer rules are mechanically checkable in CI. |

Non-goals of this ADR: implementing the layout, porting historical modules, selecting pins/peripherals/limits, defining the task/core model, fixing the ESP-IDF patch version.

## 3. Decision

### 3.1 Layer definitions

| Layer | Owns | Examples (proposed) |
|---|---|---|
| **Core** (domain policy) | Pure data types, invariants, and algorithms; no I/O; no notion of time source, threads, or hardware. | Acquisition configuration model and validation; sample/slot/frame data types; sample validity classification rules; status/fault taxonomy; wire-format *schema* encode/decode as pure functions. |
| **Application** (orchestration / use cases) | Sequencing of Core policy through Ports: lifecycle, modes, error propagation, recovery decisions, health aggregation. | Session configure/start/stop; mode state machine; fault escalation policy; command dispatch (parsed command → use case). |
| **Ports** (contracts) | Abstract interfaces required by Application (and, when needed, provided to Adapters as callbacks). Each port documents ownership, threading/ISR context, blocking behavior, and timing expectations. | `adc`, `emitter`, `clock`, `timer`, `stream`, `storage`, `command`, `log` (Section 3.5). |
| **Adapters** (platform / hardware) | Concrete port implementations on ESP-IDF/FreeRTOS/hardware; ISRs; tasks; drivers; networking; persistence; board configuration; the composition root. | SPI/I2C/GPIO/GPTimer drivers; FreeRTOS task wrappers; Wi-Fi/lwIP stream; NVS/filesystem storage; `main/app_main.c` wiring. |

### 3.2 Dependency rule

```text
Adapters ──► Ports ──► Core ◄── Application
                ▲                    │
                └────────────────────┘
```

- Application depends on Ports and Core.
- Ports depend only on Core types.
- Adapters depend on Ports, Core types, and ESP-IDF/FreeRTOS.
- Core depends on nothing project-internal and only on the C11 standard headers listed below.
- Only the composition root (`main/`) knows concrete adapters and binds them to ports.

### 3.3 Permitted / forbidden dependency matrix

Rows depend on columns (✔ allowed, ✘ forbidden).

| From \ To | Core | Ports | Application | Adapters | ESP-IDF / FreeRTOS / lwIP |
|---|---|---|---|---|---|
| Core | ✔ | ✘ | ✘ | ✘ | ✘ |
| Ports | ✔ (types) | ✔ | ✘ | ✘ | ✘ |
| Application | ✔ | ✔ | ✔ | ✘ | ✘ |
| Adapters | ✔ (types) | ✔ | ✘ ¹ | ✔ (own; others via ports) | ✔ |
| `main/` (composition root) | ✔ | ✔ | ✔ | ✔ | ✔ |

¹ Adapters must not call Application internals; inbound events reach Application only through callbacks/handles registered via Ports by the composition root.

**Forbidden includes in Core, Application, and Ports** (any match fails the Class S check):

```text
esp_*        freertos/*    driver/*    lwip/*
hal/*        soc/*         rom/*       esp_private/*
sdkconfig.h
nimble/*     host/ble_*    bt_*                         # BLE / Bluetooth
nvs.h        nvs_flash.h   nvs_*.h                      # NVS
FreeRTOS.h   task.h        queue.h     semphr.h         # unprefixed FreeRTOS
timers.h     event_groups.h
nn2_adapter_*  nn2_adapters_*                           # any adapter header
```

FreeRTOS headers are forbidden both with the `freertos/` prefix (e.g. `freertos/FreeRTOS.h`, `freertos/task.h`) and unprefixed (`FreeRTOS.h`, `task.h`, `queue.h`, `semphr.h`, `timers.h`, `event_groups.h`). Wi-Fi headers are covered by `esp_*` (e.g. `esp_wifi.h`).

**Cross-layer edge patterns** (machine-checkable, Class S FAIL on match):

| Including layer (path) | Forbidden include pattern |
|---|---|
| Core (`components/nn2_core`) | `nn2_app_*`, `nn2_port_*`, `nn2_adapter_*`, `nn2_adapters_*` |
| Ports (`components/nn2_ports`) | `nn2_app_*`, `nn2_adapter_*`, `nn2_adapters_*` |
| Application (`components/nn2_app`) | `nn2_adapter_*`, `nn2_adapters_*` |

Allowed standard headers in Core: `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<string.h>`, `<limits.h>`, and `<assert.h>` (static assertions). `<stdatomic.h>` is **not** machine-checked by Class S: whether it is acceptable depends on RTOS review, so any use is a **review-only (Class S manual)** item and is not counted as automated evidence.

**Include detection limits (Q-11):** literal scanning is bounded evidence. G3/G4/G5 are handled by the layered verification strategy in Section 3.10; full preprocessing/transitive dependency analysis remains outside the Class S gate.

Short examples:

- ✔ `nn2_app_session.c` includes `nn2_port_adc.h` and `nn2_core_frame.h`.
- ✘ `nn2_core_frame.c` includes `esp_timer.h` to timestamp a sample. Time must be passed in as a value supplied by the `clock` port via Application.
- ✘ `nn2_app_session.c` calls `xQueueSend`. Queueing belongs to an Adapter behind a port.
- ✔ `nn2_adapter_adc_esp.c` includes `driver/spi_master.h` and implements `nn2_port_adc`.

### Bounded A2 Owner decision: pinned standard-library implementation chain

The [actual Human Owner A2 decision](https://github.com/YerriSuyudHasim/noctinirs-2/pull/15#issuecomment-6306981230) (2026-10-21) clarifies §§3.3/3.10 solely for the Issue #13 / PR #15
0.1.x ring/sequencer slice on ESP32-S3, ESP-IDF v6.1 commit
`fff9895c82d744c7237be8847347bdd1b07c6643`, Xtensa GCC
`esp-15.2.0_20251204` and image
`espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.
Permitted C standard headers may transitively reach the unmodified
`components/esp_libc/platform_include/sys/cdefs.h` shim and generated `sdkconfig.h`
**solely for standard-library implementation configuration**. The assessed chain,
including `<stdint.h>`, is retained in [baseline exact-source target evidence](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39531735121/artifacts/12315877257).
Reviewed shim SHA-256: `45151a90d09f8e7d6b8acab050f2e15686e7fdca70d29b298df88af16d4d884a`.

All decision conditions remain binding:

- Product-owned inner source/headers must not include SDK configuration/platform
  headers, reference CONFIG_* symbols, select conditional SDK fallbacks, depend
  on framework/RTOS/driver APIs or adopt generated inner headers.
- SDK-free isolated host compilation, strict C11 guard/enforcement, independent
  header probes and complete host/target dependencies, commands, search roots,
  environment and configuration evidence remain required. Broad IDF-inherited
  target roots are not target isolation.
- No fake/shadow standard headers, altered SDK shim, hidden dependency evidence,
  weakened scanner/manifest/C11/build control or warning suppression is allowed.
- Only this identified standard-library chain, slice and pinned build are covered.
  SDK/toolchain/configuration changes, changed shim bytes or additional resolved
  platform headers require fresh Architect/Verifier assessment and any additional
  policy-required decision; future modules inherit no automatic waiver.
- R1/R2 retain their original conditions. No blanket transitive exception or
  timing/runtime/hardware/safety/clinical approval follows.

A1 still requires conforming opaque operational state with valid C11 caller storage,
alignment/effective type/lifetime and no heap; the proposed public-state exception
was rejected. D1 requires conditional/unmeasured blocking wording. This reviewable
source record is **not independently accepted merely because the design decision
exists**. Historical findings/evidence remain; changed candidates require fresh
applicable exact-head checks and non-author review. N5/deadline and separate
settings/ready/merge/issue/deletion/release/hardware/automation authority are unchanged.

### 3.4 C11 conventions

| Topic | Rule (accepted static framework) |
|---|---|
| Prefixes | `nn2_core_*`, `nn2_app_*`, `nn2_port_*`, `nn2_adapter_*`; macros `NN2_*`; file names match prefixes. |
| Language standard | Inner layers (`nn2_core`, `nn2_app`, `nn2_ports`) compile with `-std=c11` in both host (Class U) and target (Class B) builds. Enforced by the root build hook and compiler guard in Section 3.9 (Q-10); bounded design consultation and Owner acceptance are recorded in Section 0. Future implementation review remains required. |
| Opaque handles | Modules expose `typedef struct nn2_x nn2_x_t;` with storage supplied by the caller (static allocation helpers or `NN2_X_STORAGE_SIZE`) to avoid heap. |
| Errors | Functions return `nn2_status_t` (a Core-defined enum, `NN2_OK = 0`); outputs go through pointer parameters. Adapters translate `esp_err_t` at the boundary; `esp_err_t` never crosses into Ports/Application/Core. |
| Memory | No dynamic allocation after init. Init-time allocation is allowed only in Adapters/composition root and must be documented; Core and Application do not call `malloc`/`free`. |
| Ownership | Every buffer/handle has exactly one documented owner; borrowed pointers are valid only for the call unless the port contract states otherwise; ownership transfer is explicit in names/docs (`*_take`, `*_give`, `*_release`). |
| Ports | A port is a `struct` of function pointers plus a `void *ctx`, e.g. `typedef struct { nn2_status_t (*read)(void *ctx, ...); void *ctx; } nn2_port_adc_t;`. Ports must not expose platform types. |
| Port contract doc | Use the [port interface contract template](../templates/port-interface-contract.md). Each port header documents: caller context (task / ISR / either), blocking or nonblocking, reentrancy, ownership of arguments, error codes, and **timing expectation stated as a requirement to be measured** (not a guarantee). |
| Concurrency | Core is single-threaded and reentrant by construction (no globals with mutable state). Application state shared across contexts requires a documented synchronization strategy approved by RTOS Specialist. |
| Const / types | Fixed-width integers; `const` for inputs; no implicit platform `int` size assumptions in wire formats. |

### 3.5 Component layout and normative layer paths (NOT implemented)

```text
components/
  nn2_core/                 # pure C11; REQUIRES nothing
  nn2_ports/                # headers only; REQUIRES nn2_core
  nn2_app/                  # REQUIRES nn2_core nn2_ports
  nn2_adapters_<area>/      # e.g. _adc, _emitter, _time, _stream, _storage, _log
                            # REQUIRES nn2_ports nn2_core + ESP-IDF components
main/                       # composition root; wires adapters into app
test/host/                  # Unity host tests with fake ports (proposed)
```

**Canonical inner-layer definition:** [tools/layers.json](../../tools/layers.json) is the sole normative machine-readable policy for layer identities/paths, permitted component dependencies, header ownership prefixes, forbidden include patterns and scanned source/header suffixes. All dependency edges outside a row's `requires` are forbidden; self-layer includes are allowed, self `REQUIRES` is invalid. Empty `requires` means independent of other components. The diagrams, matrices, layout and examples in this ADR explain that policy and are not separately maintained gate configuration. CI detection, the single Python Class S scanner (via its shell wrapper), and the root C11 hook consume the manifest through [tools/layer_contract.py](../../tools/layer_contract.py).

Schema v2 has exactly `schema_version: 2`, `layers`, `source_extensions` and `forbidden_include_regex`. `layers` contains exactly one row for each semantic role `core`, `ports`, `app`, with exactly `name`, `path`, `requires` and `header_prefixes`; paths are `components/nn2_` plus the role. Dependencies reference component identities. Duplicate/unknown/self/cyclic edges, overlapping header ownership, extra/duplicate keys, repeated roles, malformed/empty policy fields, invalid regular expressions, missing/unreadable files and unsupported versions fail validation before detection or gate verdicts. JSON uses only the Python standard library. Identity/path schema checks are not an alternative dependency policy. Architectural changes to otherwise valid policy require review; regression oracles pin the intended current matrix.

The Architect owns schema/path changes through an ADR revision, independent review and Owner decision. Any new `components/nn2_*` entry (including a future adapter) requires a reviewed schema/map extension first. Detection rejects unmapped entries and malformed top-level paths. A valid manifest with absent product layers produces explicit NOT APPLICABLE (not evidence); an empty layer selects the gate and cannot establish conformance. Missing or invalid configuration fails before workflow outputs are emitted.

**Cross-PR integration (completed):** [PR #38](https://github.com/YerriSuyudHasim/noctinirs-2/pull/38), [PR #39](https://github.com/YerriSuyudHasim/noctinirs-2/pull/39), then [PR #43](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43) landed in the required order. #43 migrated the Python dependency map, CROSS patterns and platform patterns to this manifest. The legacy grep scanner is retired; wrappers invoke the same Python scanner. The [N6 integration report](../N6_INTEGRATION_REPORT.md) retains historical candidate inputs and conflict evidence; Section 0 links actual squash execution and the bounded Owner acceptance. No pending PR #40 change is included in that integration.

**`REQUIRES` / `PRIV_REQUIRES` allow-list** (derived from the layout above; no ESP-IDF components permitted):

| Component | Allowed in `REQUIRES` / `PRIV_REQUIRES` |
|---|---|
| `nn2_core` | *(none)* |
| `nn2_ports` | `nn2_core` |
| `nn2_app` | `nn2_core`, `nn2_ports` |

Any other entry is a Class S FAIL. Exact names and the split of adapter components are to be decided in the implementation issue.

Section 3.5.1 records a **Proposed** amendment for those names and for `nn2_rtos`. The accepted allow-list in the table above, and `tools/layers.json`, stay as written until that amendment is accepted and a reviewed schema extension can express it.

### 3.5.1 Proposed amendment: adapter layer, `nn2_ad_*`, and `nn2_rtos`

| Field | Value |
|---|---|
| Amendment status | **Proposed** (does not modify the Accepted status in the header or Section 0) |
| Date | 2026-10-22 |
| Author role | Agent 02 (Architect) |
| Authorization | Project Owner delegation to Nori on issue #33, [comment 6333953903](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6333953903) |
| Inputs read | `main` @ `f2d7c8cfecdc6570df155fac4d90625c16a7bf4b`; [PR #34](https://github.com/YerriSuyudHasim/noctinirs-2/pull/34) head `ac9de4a5207714b22bc102a4240ecbc5a993bae8` |
| Reviews requested | Agent 07 (independent review of this amendment on its exact head); Agent 05 (`nn2_rtos` mapping) |

This subsection extends Sections 3.1–3.5. It is binding only after independent review and a later Owner acceptance of this amendment. It authorizes no firmware edit, no flash, and no Class H claim. `tools/layer_contract.py` and `tools/class-s-gate.py` are unchanged. Their thresholds are unchanged.

#### Adapter layer

An ESP-IDF component that implements a platform, peripheral, or transport adapter is named `nn2_ad_<name>`, with `<name>` a short lowercase token. The component may depend on `nn2_ports`, `nn2_core`, and ESP-IDF or FreeRTOS components, and it may depend on `nn2_rtos` under the platform rule below. It does not depend on `nn2_app` or on another `nn2_ad_*` component. Another adapter is reached through a port.

`nn2_core`, `nn2_ports`, and `nn2_app` do not `REQUIRES` or `PRIV_REQUIRES` any `nn2_ad_*` component and do not include an `nn2_ad_*` header. The composition root (`main`, including `app_main`) links the adapter components. An `nn2_ad_*` component may link `nn2_rtos`, as defined below. No other project component links an adapter or `nn2_rtos`.

#### Both adapter prefixes are accepted

No source on `main` is renamed.

| Prefix | Home on `main` @ `f2d7c8cf` | Meaning under this amendment |
|---|---|---|
| `nn2_adapter_*` | Translation units and headers compiled into the composition root from `main/adapters/`. This includes the ADS131 adapter added in #36 (`nn2_adapter_ads131.h`, `nn2_ads131_esp.c`) and the clock, log, fake-ADC, and disabled-LED adapters. | Adapter code owned by `main`. These files are not `components/nn2_*` directories. |
| `nn2_ad_<name>` | No such component exists on this `main`. #34 adds `nn2_ad_wifi`, `nn2_ad_cred`, and `nn2_ad_prov` under `components/`. | Component name for an adapter. Each of those three names is 11 characters. |

The layout sketch `nn2_adapters_<area>` in Section 3.5 stays an illustration. The component prefix this amendment proposes is `nn2_ad_<name>`. Existing `nn2_adapter_*` files keep their names. Inner layers remain forbidden from including `nn2_adapter_*` and `nn2_adapters_*`. This amendment adds the same forbid for `nn2_ad_*` includes and for an `nn2_rtos` include from an inner layer. That extra forbid is text in this subsection. It is not yet a new `forbidden_include_regex` entry, because this amendment does not change the manifest (see below).

#### Mapped adapter components

Observed on PR #34 head `ac9de4a5`. `INCLUDE_DIRS` is `include` for each. Public headers are `nn2_ad_wifi.h`, `nn2_ad_cred.h`, and `nn2_ad_prov.h`.

| Component | Layer | `REQUIRES` on that head | Who may depend on it |
|---|---|---|---|
| `nn2_ad_wifi` | Adapter | `nn2_core`, `nn2_ports`, `nn2_rtos`, `esp_wifi`, `esp_event`, `esp_netif`, `esp_timer`, `freertos` | `main` only |
| `nn2_ad_cred` | Adapter | `nn2_core`, `nn2_ports`, `nvs_flash` | `main` only |
| `nn2_ad_prov` | Adapter | `nn2_core`, `nn2_ports`, `esp_timer` | `main` only |

#### `nn2_rtos` is platform / composition

`components/nn2_rtos` is platform/composition. It is not an inner layer and not an `nn2_ad_*` adapter. On #34 @ `ac9de4a5` its `REQUIRES` list is empty (`idf_component_register` with `SRCS` and `INCLUDE_DIRS` only).

`nn2_rtos` owns the single RTOS config header, `components/nn2_rtos/include/nn2_rtos_config.h`. That ownership matches ADR-0003 §3.6: priority, stack, and affinity macros live in one header, and they do not live in `nn2_core`, in port headers, or in scattered driver files.

| Dependent | `nn2_rtos` |
|---|---|
| `main` | Allowed |
| any `nn2_ad_*` adapter | Allowed |
| `nn2_core`, `nn2_ports`, `nn2_app` | Forbidden, including `REQUIRES`, `PRIV_REQUIRES`, and includes |

#34 @ `ac9de4a5` already has the adapter edge this table allows: `components/nn2_ad_wifi/CMakeLists.txt` `REQUIRES` `nn2_rtos`, and `components/nn2_ad_wifi/nn2_ad_wifi.c` includes `nn2_rtos_config.h`. `nn2_core`, `nn2_ports`, and `nn2_app` do not list `nn2_rtos` and do not include that header. The rule is this edge. It does not add dependents beyond `main` and `nn2_ad_*`.

#### N-1: duplicate `nn2_rtos_config.h` (follow-up for #34, not mapped)

#34 has two headers of this name. Which file a quoted `#include "nn2_rtos_config.h"` opens depends on include order and on the quoting translation unit's directory.

| Path | This amendment |
|---|---|
| `components/nn2_rtos/include/nn2_rtos_config.h` | The single RTOS config header, owned by `nn2_rtos` |
| `main/adapters/nn2_rtos_config.h` | Already on `main` @ `f2d7c8cf`. ADS131 diagnostic macros (`NN2_RTOS_ADS131_*`). Follow-up **N-1** for #34. Not a second owner. Not mapped. |

On that head, `main/nn2_wifi_bringup.c` includes the component header by a relative path, and `main/adapters/nn2_ads131_esp.c` includes the diagnostic header beside that translation unit. N-1 is the removal of the duplicate name. This amendment does not move the ADS131 macros.

#### Platform tuning stays in the owning adapter

Values that come from ESP-IDF or Kconfig live only in the owning adapter, as that adapter's compile definitions or as macros in that adapter's own header. Host defaults for those values live in a test-only header or in test-only flags.

The values named by this rule are `NN2_WIFI_BACKOFF_FIRST_MS`, `NN2_WIFI_BACKOFF_MAX_MS`, `NN2_WIFI_BACKOFF_FACTOR`, and `NN2_WIFI_CONNECT_GUARD_MS`. On #34 @ `ac9de4a5`, `nn2_ad_wifi` injects the target numbers, and `components/nn2_core/include/nn2_wifi_limits.h` still provides `#ifndef` host defaults for the same macros. This amendment does not approve those defaults in `nn2_core`. Domain sizes that are not platform tuning (SSID and passphrase lengths in that header) are outside this paragraph. Relocating the host defaults is a #34 follow-up. It is not a change to either gate script.

#### Why `tools/layers.json` is unchanged

Schema v2, as `tools/layer_contract.py` validates it today, has exactly three `layers` rows, roles `core`, `ports`, and `app`, and paths `components/nn2_` plus the role. Any other count, role, path, or top-level key fails `load_contract` before `detect`. `detect` then fails every `components/nn2_*` directory outside those three paths with `unmapped component`.

Adding `nn2_ad_wifi`, `nn2_ad_cred`, `nn2_ad_prov`, or `nn2_rtos` to `tools/layers.json` would fail that validation on `main` as well as on #34. That is a regression of the amendment branch by itself. This amendment leaves the manifest byte-for-byte as on `main`. The inner-layer `requires` lists, `forbidden_include_regex`, source extensions, and schema version stay put.

The tables above are the Proposed map. They become gate inputs only through a later reviewed schema extension that can tell an adapter row from an inner-layer row. Until then, an unmapped `components/nn2_*` directory remains a failure of `layer_contract.py detect` and of `class-s-gate.py`.

Two further facts belong in that later review, and they are recorded so this amendment is not read as permission to drop them into the current three rows:

- The include scanner applies one `forbidden_include_regex` list to every manifest row. That list contains `sdkconfig.h`, `esp_*`, `freertos/*`, and `nvs.h`. `nn2_rtos` includes `sdkconfig.h` when `ESP_PLATFORM` is set. Each `nn2_ad_*` component includes ESP-IDF or FreeRTOS headers. Those includes are in bounds for an adapter or for `nn2_rtos` under this amendment, and they remain out of bounds for `nn2_core`, `nn2_ports`, and `nn2_app`.
- The CMake scanner accepts a component file that is only one literal `idf_component_register`, with no `$` expansion. `components/nn2_ad_wifi/CMakeLists.txt` on #34 also contains `if()`, `target_compile_definitions` using `${CONFIG_...}`, and `target_compile_options`. `nn2_ad_cred`, `nn2_ad_prov`, and `nn2_rtos` each contain `target_compile_options` after registration.

Neither fact is fixed here. Neither gate is weakened to ignore it.

The duplicate header (N-1) and the `nn2_wifi_limits.h` host defaults are not inputs to `detect` or to `class-s-gate.py`. Stating them in this subsection does not change either verdict. `class-s-gate.py` calls `detect` before the include audit and before the `REQUIRES` audit, so an unmapped component ends the run before those audits see `nn2_ad_wifi`'s edge to `nn2_rtos`.

### 3.6 Candidate ports (proposed)

| Port | Purpose | Key contract questions |
|---|---|---|
| `adc` | Deliver raw sample records (with timestamps) to Application. | Push (callback) vs pull; batch size; ISR vs task context. |
| `emitter` | Request emitter states/intensities; report completion/fault. | Fail-safe OFF semantics; asynchronous completion; limits are Adapter config (Hardware review). |
| `clock` / `timebase` | Monotonic time value (e.g. 64-bit microseconds). | Resolution, wrap, cross-core consistency. |
| `timer` | Schedule deadline events. | Absolute vs relative deadlines; jitter expectations (to be measured). |
| `stream` | Emit encoded records to a host transport. | Nonblocking; drop-and-count policy; buffer ownership. |
| `storage` | Persist configuration/calibration data. | Atomicity; wear; scope for 0.1.x (Owner). |
| `command` | Deliver parsed host commands to Application. | Transport-independent framing; authentication scope. |
| `log` | Structured logging/diagnostics. | Must be safe to call from which contexts; never in ISR by default. |

The list is a candidate set. Each port requires its own interface specification and review before implementation.

### 3.7 Real-time path analysis

The acquisition path (sample-ready event → sample capture → classification → frame assembly) is expected to be timing-sensitive. Layering adds costs that must be evaluated, not assumed:

1. **Indirect-call overhead.** Function-pointer port calls prevent inlining and add per-call overhead. Its significance at the required sample rate is **unknown** and must be measured on target.
2. **Mitigation A: bulk/batch ports.** Prefer port calls that move batches (e.g. N samples per call, ring-buffer handoff) so indirection cost is amortized and Core processing runs in task context.
3. **Mitigation B: static binding.** For hot-path ports, allow link-time binding (single implementation selected by build) while keeping the same header contract, so host tests can link fakes.
4. **Mitigation C: real-time island exception.** A narrowly scoped Adapter-side component may implement a hard-real-time loop directly on ESP-IDF/FreeRTOS, producing Core data types into a port. This exception must be recorded in its own ADR, with scope, justification, measurement, and RTOS Specialist review. Core rules never relax.

No timing, latency, jitter, or throughput claim is made by this ADR. All three mitigations require RTOS Specialist input and on-target measurement (Class H, hardware/on-target evidence) before adoption. Class B (build) is not sufficient for any timing claim.

### 3.8 Error propagation and recovery

- Core reports violations as `nn2_status_t` values and never aborts, logs, or retries.
- Adapters convert platform errors to `nn2_status_t`, preserve the raw platform code in a diagnostic counter or log via the `log` port, and never decide system-level recovery.
- Application owns recovery policy: classify (transient / session-fatal / system-fatal), count, decide (retry with bound, invalidate data, stop session, request safe state), and expose state through health/diagnostic ports.
- Safe-state actions (e.g. emitters OFF) are requested through ports; the concrete fail-safe mechanism and its timing are Hardware/RTOS decisions.
- `assert` / `abort` are reserved for programmer errors in debug builds; policy is TBD with RTOS Specialist (watchdog interaction).

### 3.9 Q-10 decision: verifiable ISO C11 enforcement

Decision accepted within Section 0's static framework: strict ISO C11 for inner-layer C translation units and every port header, in host and ESP-IDF builds. Adapters and the composition root retain the SDK default. The bounded Firmware/RTOS consultation supports this design; future implementation, atomic and timing approvals remain separate.

At ESP-IDF v6.1 commit `fff9895c82d744c7237be8847347bdd1b07c6643`, `tools/cmake/build.cmake` selects `-std=gnu23` for the target compiler. The older Issue #11 comment's gnu17 default is not this pinned source's behavior. `C_STANDARD 11` alone is insufficient: CMake may decay unless `C_STANDARD_REQUIRED` is set, extensions default ON, and raw SDK/compiler options can override properties.

Implementation: root `CMakeLists.txt` includes [cmake/nn2_c11.cmake](../../cmake/nn2_c11.cmake) **after** `project()`, when IDF component targets exist. `nn2_require_c11(target)` sets `C_STANDARD 11`, `C_STANDARD_REQUIRED YES`, `C_EXTENSIONS NO`, and C-only private `-std=c11 -pedantic-errors`. A forced [guard header](../../cmake/nn2_c11_guard.h) requires `__STDC_VERSION__ == 201112L` and `__STRICT_ANSI__`. A later C17 or GNU11 source override therefore causes compilation to fail. Unsupported compiler families fail closed. Root placement avoids adding commands to component CMake files rejected by PR #38's restricted parser.

The root hook uses the canonical paths and compiles one independent object probe per `.h` in each existing layer, including header-only Ports. Each probe inherits the owning component's public/transitive requirements and private include directories and compile definitions. Repository-relative header paths are SHA-256 hashed for distinct probe filenames and target identities; `nn2_c11_header_probes.tsv` records coverage. Unrelated private headers are never required to coexist in one artificial translation unit; intentional combined-header/include-order checks remain separate review or test work. Probes are build checks and are not linked into product firmware. Header self-containment and per-header include order still require review and host tests. Future host build targets must call the same helper and compile all port headers with only inner-layer include directories, standard library and explicit mocks. Product Class U tests must execute a nonzero test count; these contract fixtures do not constitute product Class U evidence.

Verification: `tests/architecture/test_c11.py` builds CMake fixtures with GCC/Clang on host and with the pinned Xtensa compiler in CI: ISO C11 features succeed; later C17/GNU11 overrides and GNU statement expressions fail. `tools/verify_c11_idf.py` creates synthetic components outside the product tree, exercises actual IDF component registration, builds separate private-header source TUs plus header-only Ports, and checks a one-to-one header/probe inventory and the exact expected source/probe compile-command coverage with effective `-std=c11`. Helper-level regressions cover private namespace conflicts, matching include guards, component context and collision-safe identities. `tools/verify_c11_regression.py` confirms that both F1 regressions fail against the unchanged reviewed helper before checking the corrected implementation. CI retains generated probe sources, fixture headers, inventories, compile commands, logs and checksums for 90 days. Normal Class B still builds the unchanged product scaffold. No production-layer C11 conformance is claimed while layers are absent.

Limits: C11 flags/guard do not prove all code is portable, detect deliberate macro spoofing, inspect link-time symbols, or constrain arbitrary project/toolchain mutations. GNU `__extension__`, suppression pragmas, C++/assembly additions, forced includes, target/source options and compiler wrappers require manual review. Only GCC/Clang toolchains are supported by this implementation. New host platforms need explicit verification. A missing compiler is a test failure, not a silent skip.

### 3.10 Q-11 decision: complementary include/dependency verification

The Verifier owns mechanical evidence; the Architect owns dependency interpretation; Firmware owns build inputs; RTOS reviews atomic/concurrency exceptions. No layer may use a scanner blind spot as permission to depend outward.

| Mechanism | Class S guarantee / limitation | Required complementary verification and responsibility |
|---|---|---|
| Literal direct includes | The integrated Python scanner performs PR #38's lexical/path normalization and derives inward edges and forbidden patterns from the canonical manifest. It is not a full preprocessor. | Verifier runs the exact-head gate/fixtures; Reviewer compares supported patterns with Section 3.3. Conservative scanning can flag inactive preprocessor branches (false positives); rewrite or request a reviewed exception, never bypass CI. |
| Macro-expanded includes (G3), macro-built platform calls | No expansion guarantee; a literal scan can miss them (false negatives). | Firmware/Verifier compile all inner source and public headers on host with only permitted include roots; exercise configuration branches. Reviewer inspects macros and call targets. Disabled/unbuilt branches remain manual risk. |
| `__has_include` | May hide an outward include or select a fallback that makes an isolated host build pass. No Class S guarantee. | Reviewer inspects every conditional include/probe and supported configuration; Verifier builds both relevant branches where executable. Conditional platform dependencies in inner layers are forbidden, even if absent on host. |
| Generated headers (G5) | Scanner sees only files present when run; generation can occur later. | Generated headers in inner layers are unsupported by default. Firmware must propose an ADR exception identifying generator, reproducibility, output roots, provenance and post-generation scan/build before adoption. Product-owned `sdkconfig.h` dependencies remain forbidden; the sole pinned standard-library implementation chain is distinguished by the bounded A2 Owner decision above. |
| Transitive includes (G4) | Direct scan does not compute a transitive graph. PR #38's explicit REQUIRES audit checks declared component names only. | Verifier's future product host build must use restricted include roots and retain compiler `-M`/dependency output including system headers (`-MM` alone can omit `-isystem` headers). Class B compile commands/dependency files are reviewed against allowed roots. Link symbols/callback targets remain manual architecture review. |
| Include directories / search order | A legal header name can resolve to an unintended platform/header shadow. | Firmware records compiler, working directory, `-I`, `-isystem`, `-iquote`, forced includes, sysroot and environment (`CPATH` etc.). Verifier retains compile commands and resolved dependencies; Reviewer checks provenance/search order, system-header classification and mocks. Host isolation is complementary, not proof across target configurations. |
| Build-generated/implicit dependencies | Textual CMake audit cannot evaluate arbitrary toolchain files, target mutations, Component Manager or build graph generators. PR #38 deliberately rejects unsupported component syntax. | Reviewer audits root/toolchain/build changes and generated dependency graph. Verifier performs clean host/target builds of supported configurations and retains generated evidence; new dynamic mechanisms require a reviewed contract extension. |

The bounded A2 decision does not remove the standard-header/transitive review obligation.
The literal scanner remains unchanged: product includes of `sdkconfig.h` still fail;
only the identified libc implementation chain is conditionally permitted. Each new
candidate must retain its complete target graph and check that no additional platform
header or product CONFIG_* dependence has appeared.

**What is executed now:** Class S enforcement is preserved through canonical policy acquisition; manifest/detection regressions, synthetic C11 host/target checks and actual IDF fixture are automated. **What is required when product layers land:** isolated host build of all inner TUs/headers, retained full compiler dependencies and compile-command review, branch/configuration coverage and port acceptance tests. No product dependency-graph checker or host unit suite is introduced here. **Deferred/unsupported:** comprehensive preprocessor/CMake interpretation, exhaustive feature-branch expansion and automated link/callback reachability. Residual risk is explicit manual architecture review until a separately reviewed implementation closes it. Class S success alone cannot close G3/G4/G5 or prove absence of transitive/platform dependencies.

## 4. Testability and integration contracts

| Class | Check | Scope | Notes |
|---|---|---|---|
| **S** (Static) | Include grep: fail on forbidden patterns and cross-layer edges (Section 3.3) in the normative paths (Section 3.5). | Layer rule | Fast; no toolchain required. Macro/transitive/generated cases deferred (G3/G4/G5). |
| **S** (Static) | `REQUIRES`/`PRIV_REQUIRES` audit of `CMakeLists.txt` against the allow-list (Section 3.5). | Layer rule | Catches dependencies grep misses. |
| **S** (manual) | Review-only items (e.g. `<stdatomic.h>` use). | Layer rule | Reviewer record; not automated evidence. |
| **U** (Host/unit) | Unity tests built with host compiler, linking Core/Application against fake ports. | Logic | No hardware or timing claims. |
| **B** (Build) | `idf.py set-target esp32s3 && idf.py build` with the ESP-IDF version pinned by Issue #2. | Compile/link | Not hardware validation. |
| **H** (Hardware/on-target) | Measurement on ESP32-S3 hardware. | Timing, behavior | Required for any timing/latency/jitter claim. Out of scope for this ADR. |

These run as **separate CI jobs** so that a failure is attributable to one class. Job definitions, runner images, and the ESP-IDF pin are coordinated with the Verifier under Issue #2. Rev 2 adds contract fixture CI; existing production S/B semantics remain.

**Result semantics (normative):**

- A **NOT APPLICABLE** or vacuous result (e.g. a grep over zero files, a skipped job) is **not evidence** and must never be reported as green/pass.
- A skipped gate is recorded as **NOT APPLICABLE (not evidence)**.
- Tool errors are **FAIL**, not pass (e.g. `grep` exit status >= 2, unreadable `CMakeLists.txt`, parse error).
- If any normative path (Section 3.5) exists, the Class S gate **must run and succeed**; a skipped or non-successful gate in that state is FAIL.

**Acceptance criteria per class:**

| Class | Pass requires |
|---|---|
| S (include grep) | Gate ran over every existing normative path; zero matches; no tool error. |
| S (`REQUIRES` audit) | Every `CMakeLists.txt` under the normative paths parsed; every `REQUIRES`/`PRIV_REQUIRES` entry within the allow-list; any entry outside it = FAIL; parse failure = FAIL. |
| U (host unit) | Core and Application built with the host compiler (`-std=c11`) against fake ports; tests actually executed (non-zero test count); all pass; logs retained as CI artifacts with run ID. |
| B (build) | As defined under Issue #2. |
| H (on-target) | Defined per measurement issue; not claimed by this ADR. |

## 5. Alternatives considered

| Alternative | Description | Pros | Cons | Outcome |
|---|---|---|---|---|
| A. Component-per-peripheral | Historical V2/V3/V4 style: one component per subsystem with IDF inside. | Familiar; minimal indirection. | Policy mixed with hardware; no host testing; boundaries unenforceable. | Rejected. |
| B. Two-zone (Core + Adapters) | No Application layer. | Fewer modules. | Lifecycle/recovery policy leaks into adapters or bloats Core with orchestration. | Rejected. |
| C. FreeRTOS-aware Application | Application may use FreeRTOS primitives directly. | Easier RT orchestration. | Loses host testing of orchestration; couples to RTOS. | Rejected as default; RT needs handled via 3.7. |
| D. OS abstraction port (OSAL) | Application uses an `nn2_port_os` for tasks/queues/mutexes. | Host-testable concurrency. | Large surface; risk of a leaky RTOS clone. | Deferred; may be revisited with RTOS Specialist. |
| E. C++ interfaces | Virtual classes / templates for ports. | Type-safe interfaces. | Contradicts C-3 (C11). | Rejected by Owner decision. |

## 6. Consequences

Positive: host-testable Core/Application; mechanically checkable boundaries; reviewable contracts per port; historical influence becomes explicit and traceable.

Negative: more components and boilerplate; indirect-call overhead on hot paths (to be measured); port specifications must be written before drivers; possible need for a documented real-time island exception.

Neutral: `main/app_main.c` becomes the only place that knows concrete adapters.

## 7. Affected components and interfaces

- `CMakeLists.txt` (root) and `main/`: future composition root changes (separate issue).
- Future `components/nn2_*` (proposed, Section 3.5).
- Future port headers (Section 3.6).
- Future CI jobs S/U/B (coordinated with Issue #2).
- Rev 2 adds canonical configuration, a root C11 hook, compiler fixtures and CI wiring; no firmware behavior or `reference/**` changes.

## 8. Verification strategy (mapped to Issue #3)

| AC | How this ADR satisfies it | Evidence |
|---|---|---|
| AC-1 | Problem (1.1), decision (3), consequences (6), alternatives (5). | Static review |
| AC-2 | Four layers and responsibilities (3.1), C11 conventions (3.4). | Static review |
| AC-3 | Dependency rule (3.2), matrix and forbidden includes (3.3). | Static review |
| AC-4 | Non-adoption rule (1.3). | Static review |
| AC-5 | Scaffold gap (1.2); "not implemented" statements throughout. | Static review |
| AC-6 | The PR adds only this file; no source layout or `reference/**` change. | PR diff |

The bounded framework's independent review and Owner acceptance are recorded in Section 0. This decision-record revision requires its own non-author review and subsequent Owner merge authorization. Product code conformance is verified later by the Class S/U/B jobs; it is not established by document acceptance.

## 9. Open questions and risks

| # | Question / risk | Owner of answer |
|---|---|---|
| Q-1 | Exact ESP-IDF version and toolchain pin. | Verifier / Firmware (Issue #2) |
| Q-2 | Board revision and pin map, including historical GPIO35–37 vs Octal PSRAM concern (`reference/v3/README.md`). | Hardware Specialist |
| Q-3 | Emitter current, optical exposure limits, and fail-safe OFF mechanism. | Hardware Specialist + Owner |
| Q-4 | Task/core model, priorities, ISR contract, queue vs ring handoff, watchdog policy. | RTOS Specialist |
| Q-5 | Acceptability of function-pointer ports on the hot path; need for a real-time island exception. | RTOS Specialist (measurement) |
| Q-6 | Product scope for 0.1.x (modalities, transport, storage). | Owner |
| Q-7 | Host test framework (Unity) as a dependency: provenance/licence review. | Owner / Verifier |
| Q-8 | Historical protection gap superseded by active main ruleset 25220606; meaningful Class U restoration remains governed by N5 in Section 0. This record changes no protection. | Owner / governance |
| Q-9 | Backup owner unassigned. | Owner |
| Q-10 | Static enforcement design accepted after bounded Firmware/RTOS consultation (Section 0); future product compiler evidence and implementation review remain required. | Firmware + RTOS Specialist / Verifier |
| Q-11 | Strategy resolved in 3.10; production host/dependency evidence deferred until layers land. | Verifier / Architect |

## 10. References

- [Issue #11 Owner decision packet](../M1_ISSUE11_DECISION_PACKET.md) and [port interface template](../templates/port-interface-contract.md).
- [Pinned ESP-IDF build.cmake](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/tools/cmake/build.cmake) (C dialect flags).
- CMake [C_STANDARD](https://cmake.org/cmake/help/v4.0/prop_tgt/C_STANDARD.html), [C_STANDARD_REQUIRED](https://cmake.org/cmake/help/v4.0/prop_tgt/C_STANDARD_REQUIRED.html), [C_EXTENSIONS](https://cmake.org/cmake/help/v4.0/prop_tgt/C_EXTENSIONS.html).


- Issues: #1 (M0.1 Governance), #2 (M0.2 Reproducible ESP-IDF build), #3 (this ADR), #4 (M0.4 Historical reference assessment).
- Owner decision OWNER-M0-A1-001 r1.1 (Core / Application / Ports / Adapters naming; C11).
- PR #5 (M0.1 governance fields, OWNER-M0-A1-001), merged as `52d9f85d6ce454b7e1ef97cc7908bbae92b49d4f`.
- `AGENTS.md`, `project.yaml`, `README.md`, `CMakeLists.txt`, `main/app_main.c`, `main/CMakeLists.txt`, `sdkconfig.defaults` @ `52d9f85`.
- `ai-engineering-hub` `AGENTS.md` and `projects/noctinirs-2/project.yaml` @ `e645829a726b1008c46373d7ef2b25929988d7f1`.
- Evidence only (not requirements), @ `b1c48083c90368aed126b447eea3a89a29ecad73`: `reference/v3/docs/V3_ARCHITECTURE_CONTRACT.md`, `reference/v3/docs/V3_P5_ARCHITECTURE.md`, `reference/v3/README.md`, `reference/v2/README.md`, `reference/v4/.github/copilot-instructions.md`.

## 11. Revision history

| Rev | Date | Change | Status |
|---|---|---|---|
| 0 | 2026-10-16 | Initial draft (`c5667a46`). | Proposed |
| 1 | 2026-10-16 | Revision following the PR #6 architecture review (head `690deff`) and the Orchestrator decision in the team room: normative layer paths (3.5); NOT APPLICABLE is not evidence (4); `-std=c11` for inner layers; explicit FreeRTOS/BLE/NVS include patterns and cross-layer edges (3.3); `<stdatomic.h>` restated as review-only; `REQUIRES` allow-list; Class U and `REQUIRES` audit acceptance criteria; Class H for on-target claims (3.7); OWNER-M0-A1-001 r1.1 cited. | **Proposed** (unchanged) |
| 2 | 2026-10-17 | Issue #11: Q-10 strict C11 root enforcement and compiler probes; Q-11 verification/residual-risk strategy; JSON canonical manifest and partial cross-PR N6 integration; port contract template and Owner decision packet. | **Proposed** (Owner decision required) |

| 3 | 2026-10-17 | N6 combined integration: schema v2 owns dependency/include policy, hardened Python scanner and canonical detector replace independent gate maps; adversarial and CI propagation regressions. | **Proposed** (Owner decision required) |
| 4 | 2026-10-19 | Record the actual 2026-10-18 Owner acceptance of the reviewed static framework, source links and retained conditions; update current decision dispositions without altering historical evidence. | **Accepted** (bounded scope in Section 0) |
| 6 | 2026-10-22 | Proposed amendment in Section 3.5.1 (branch `docs/adr-0001-amend-adapter-layer`). The date line already assigns revision 5 to the 2026-10-21 A2 clarification in the body; this table does not reuse that number. Extends the accepted contract with an adapter layer named `nn2_ad_<name>`, keeps existing `main/adapters/nn2_adapter_*` names (including the #36 ADS131 adapter) with no source rename, maps `nn2_ad_wifi`, `nn2_ad_cred`, and `nn2_ad_prov`, and maps `nn2_rtos` as platform/composition reachable from `main` and from `nn2_ad_*` only. `nn2_rtos` owns the single RTOS config header (ADR-0003). The duplicate `main/adapters/nn2_rtos_config.h` is #34 follow-up N-1 and is not mapped. IDF/Kconfig tuning defaults stay out of `nn2_core`. `tools/layers.json` and both gate scripts are unchanged, so #34 head `ac9de4a5` still fails `detect` with `unmapped component` until a reviewed schema extension. Refs [#34](https://github.com/YerriSuyudHasim/noctinirs-2/pull/34) and this amendment pull request. | **Proposed** |
