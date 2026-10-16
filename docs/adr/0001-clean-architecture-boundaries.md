# ADR-0001: Clean Architecture Layer Boundaries for NoctiNIRS 2

| Field | Value |
|---|---|
| ID | ADR-0001 |
| Title | Clean architecture layer boundaries (Core / Application / Ports / Adapters) |
| Status | **Proposed** (not approved; not implemented) |
| Date | 2026-10-16 |
| Issue | #3 (M0.3 — Clean Architecture ADR) |
| Author role | Architect (draft) |
| Independent reviewer | TBD (must not be the author) |
| Approver / merge | Project Owner `YerriSuyudHasim` only |
| Decision authority | OWNER-M0-A1-001 (recorded in `project.yaml` `governance.decision_ref`) |
| Input commit | `noctinirs-2` `main` @ `52d9f85d6ce454b7e1ef97cc7908bbae92b49d4f` (merge of PR #5) |
| Governance input | `ai-engineering-hub` `main` @ `e645829a726b1008c46373d7ef2b25929988d7f1` (read 2026-10-16) |
| Evidence class of this document | Static (design review only) |

> This ADR describes a *target* architecture. Nothing in it is implemented at the input commit. Proposed names, paths, and interfaces are not approved until this ADR is accepted by the Project Owner after independent review.

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
| C-3 | Language: **C11**. |
| C-4 | Layer naming: **Core / Application / Ports / Adapters**. |
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
hal/*        soc/*         sdkconfig.h
```

Also forbidden in these layers: `rom/*`, `nvs*`, `esp_private/*`, Wi-Fi/BLE headers, and any `nn2_adapter_*` header. Allowed standard headers in Core: `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<string.h>`, `<limits.h>`, `<stdatomic.h>` (only if RTOS review accepts it), and `<assert.h>` (static assertions).

Short examples:

- ✔ `nn2_app_session.c` includes `nn2_port_adc.h` and `nn2_core_frame.h`.
- ✘ `nn2_core_frame.c` includes `esp_timer.h` to timestamp a sample. Time must be passed in as a value supplied by the `clock` port via Application.
- ✘ `nn2_app_session.c` calls `xQueueSend`. Queueing belongs to an Adapter behind a port.
- ✔ `nn2_adapter_adc_esp.c` includes `driver/spi_master.h` and implements `nn2_port_adc`.

### 3.4 C11 conventions

| Topic | Rule (proposed) |
|---|---|
| Prefixes | `nn2_core_*`, `nn2_app_*`, `nn2_port_*`, `nn2_adapter_*`; macros `NN2_*`; file names match prefixes. |
| Opaque handles | Modules expose `typedef struct nn2_x nn2_x_t;` with storage supplied by the caller (static allocation helpers or `NN2_X_STORAGE_SIZE`) to avoid heap. |
| Errors | Functions return `nn2_status_t` (a Core-defined enum, `NN2_OK = 0`); outputs go through pointer parameters. Adapters translate `esp_err_t` at the boundary; `esp_err_t` never crosses into Ports/Application/Core. |
| Memory | No dynamic allocation after init. Init-time allocation is allowed only in Adapters/composition root and must be documented; Core and Application do not call `malloc`/`free`. |
| Ownership | Every buffer/handle has exactly one documented owner; borrowed pointers are valid only for the call unless the port contract states otherwise; ownership transfer is explicit in names/docs (`*_take`, `*_give`, `*_release`). |
| Ports | A port is a `struct` of function pointers plus a `void *ctx`, e.g. `typedef struct { nn2_status_t (*read)(void *ctx, ...); void *ctx; } nn2_port_adc_t;`. Ports must not expose platform types. |
| Port contract doc | Each port header documents: caller context (task / ISR / either), blocking or nonblocking, reentrancy, ownership of arguments, error codes, and **timing expectation stated as a requirement to be measured** (not a guarantee). |
| Concurrency | Core is single-threaded and reentrant by construction (no globals with mutable state). Application state shared across contexts requires a documented synchronization strategy approved by RTOS Specialist. |
| Const / types | Fixed-width integers; `const` for inputs; no implicit platform `int` size assumptions in wire formats. |

### 3.5 Proposed component layout (proposed, NOT implemented)

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

ESP-IDF `idf_component_register(... REQUIRES ...)` lists for `nn2_core`, `nn2_ports`, and `nn2_app` must contain only allow-listed project components (no IDF components). Exact names and the split of adapter components are to be decided in the implementation issue.

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

No timing, latency, jitter, or throughput claim is made by this ADR. All three mitigations require RTOS Specialist input and on-target measurement (Class B / on-target evidence) before adoption.

### 3.8 Error propagation and recovery

- Core reports violations as `nn2_status_t` values and never aborts, logs, or retries.
- Adapters convert platform errors to `nn2_status_t`, preserve the raw platform code in a diagnostic counter or log via the `log` port, and never decide system-level recovery.
- Application owns recovery policy: classify (transient / session-fatal / system-fatal), count, decide (retry with bound, invalidate data, stop session, request safe state), and expose state through health/diagnostic ports.
- Safe-state actions (e.g. emitters OFF) are requested through ports; the concrete fail-safe mechanism and its timing are Hardware/RTOS decisions.
- `assert` / `abort` are reserved for programmer errors in debug builds; policy is TBD with RTOS Specialist (watchdog interaction).

## 4. Testability and integration contracts

| Class | Check | Scope | Notes |
|---|---|---|---|
| **S** (Static) | Include grep: fail on forbidden patterns (Section 3.3) in `components/nn2_core`, `nn2_ports`, `nn2_app`. | Layer rule | Fast; no toolchain required. |
| **S** (Static) | `REQUIRES`/`PRIV_REQUIRES` audit of `CMakeLists.txt` against an allow-list per layer. | Layer rule | Catches dependencies grep misses. |
| **U** (Host/unit) | Unity tests built with host compiler, linking Core/Application against fake ports. | Logic | No hardware or timing claims. |
| **B** (Build) | `idf.py set-target esp32s3 && idf.py build` with the ESP-IDF version pinned by Issue #2. | Compile/link | Not hardware validation. |

These run as **separate CI jobs** so that a failure is attributable to one class. Job definitions, runner images, and the ESP-IDF pin are coordinated with the Verifier under Issue #2. This ADR does not add CI.

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
- No change to `reference/**`, build configuration, or firmware in this ADR.

## 8. Verification strategy (mapped to Issue #3)

| AC | How this ADR satisfies it | Evidence |
|---|---|---|
| AC-1 | Problem (1.1), decision (3), consequences (6), alternatives (5). | Static review |
| AC-2 | Four layers and responsibilities (3.1), C11 conventions (3.4). | Static review |
| AC-3 | Dependency rule (3.2), matrix and forbidden includes (3.3). | Static review |
| AC-4 | Non-adoption rule (1.3). | Static review |
| AC-5 | Scaffold gap (1.2); "not implemented" statements throughout. | Static review |
| AC-6 | The PR adds only this file; no source layout or `reference/**` change. | PR diff |

Acceptance requires an independent reviewer and Owner acknowledgment. Code conformance is verified later by the Class S/U/B jobs.

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
| Q-8 | Branch protection on `main` is reported unavailable/disabled (`protected: false`); merge discipline relies on process. | Owner (Issue #1 follow-up) |
| Q-9 | Backup owner unassigned. | Owner |

## 10. References

- Issues: #1 (M0.1 Governance), #2 (M0.2 Reproducible ESP-IDF build), #3 (this ADR), #4 (M0.4 Historical reference assessment).
- PR #5 (M0.1 governance fields, OWNER-M0-A1-001), merged as `52d9f85d6ce454b7e1ef97cc7908bbae92b49d4f`.
- `AGENTS.md`, `project.yaml`, `README.md`, `CMakeLists.txt`, `main/app_main.c`, `main/CMakeLists.txt`, `sdkconfig.defaults` @ `52d9f85`.
- `ai-engineering-hub` `AGENTS.md` and `projects/noctinirs-2/project.yaml` @ `e645829a726b1008c46373d7ef2b25929988d7f1`.
- Evidence only (not requirements), @ `b1c48083c90368aed126b447eea3a89a29ecad73`: `reference/v3/docs/V3_ARCHITECTURE_CONTRACT.md`, `reference/v3/docs/V3_P5_ARCHITECTURE.md`, `reference/v3/README.md`, `reference/v2/README.md`, `reference/v4/.github/copilot-instructions.md`.
