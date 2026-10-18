# ADR-0001: Clean Architecture Layer Boundaries for NoctiNIRS 2

| Field | Value |
|---|---|
| ID | ADR-0001 |
| Title | Clean architecture layer boundaries (Core / Application / Ports / Adapters) |
| Status | **Proposed** (not approved; not implemented) |
| Date | 2026-10-17 (rev 2, see Revision history) |
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

### 3.4 C11 conventions

| Topic | Rule (proposed) |
|---|---|
| Prefixes | `nn2_core_*`, `nn2_app_*`, `nn2_port_*`, `nn2_adapter_*`; macros `NN2_*`; file names match prefixes. |
| Language standard | Inner layers (`nn2_core`, `nn2_app`, `nn2_ports`) compile with `-std=c11` in both host (Class U) and target (Class B) builds. Enforced by the root build hook and compiler guard in Section 3.9 (Q-10); specialist review remains required before Owner acceptance. |
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

**Canonical inner-layer definition:** [tools/layers.json](../../tools/layers.json) is the sole maintained normative path list. The layout above and path examples elsewhere explain the architecture; they are not independent configuration. CI detection, the main-line shell gate, and the root C11 hook consume the manifest through [tools/layer_contract.py](../../tools/layer_contract.py).

Schema v1 is an object with exactly `schema_version: 1` and `layers`, an array containing exactly one object for each semantic role `core`, `ports`, `app`. Each row has exactly `name` and `path`; its path must be `components/nn2_` plus its name. Extra keys, duplicate JSON keys, repeated roles, missing/empty/unreadable files, unsupported versions, unexpected roles or paths fail validation. JSON uses only the Python standard library; no YAML dependency is added. The role/path relationship is schema validation, not a second configurable path map.

The Architect owns schema/path changes through an ADR revision, independent review and Owner decision. Any new `components/nn2_*` entry (including a future adapter) requires a reviewed schema/map extension first. Detection rejects unmapped entries and malformed top-level paths. A valid manifest with absent product layers produces explicit NOT APPLICABLE (not evidence); an empty layer selects the gate and cannot establish conformance. Missing or invalid configuration fails before workflow outputs are emitted.

**Cross-PR dependency:** Issue #10 [PR #38](https://github.com/YerriSuyudHasim/noctinirs-2/pull/38) @ `71bd7ae4413ce06cf12d24b384ebac554e3a647a` is unmerged. Its Python gate maintains its own `LAYERS` map. This revision does not import, alter or replace that implementation. Full N6 integration remains **PARTIAL** until the Integration Agent changes that gate to obtain paths from this manifest, preserving its dependency allow-lists and all fixtures. Resolve the shared workflow/shell-wrapper conflict explicitly; rerun its 51 Python methods, Bash suite, detection/invariant fixtures and Class B build. See the [decision packet](../M1_ISSUE11_DECISION_PACKET.md). No cross-PR success is inferred.

**`REQUIRES` / `PRIV_REQUIRES` allow-list** (derived from the layout above; no ESP-IDF components permitted):

| Component | Allowed in `REQUIRES` / `PRIV_REQUIRES` |
|---|---|
| `nn2_core` | *(none)* |
| `nn2_ports` | `nn2_core` |
| `nn2_app` | `nn2_core`, `nn2_ports` |

Any other entry is a Class S FAIL. Exact names and the split of adapter components are to be decided in the implementation issue.

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

Decision proposed for Owner acceptance: strict ISO C11 for inner-layer C translation units and every port header, in host and ESP-IDF builds. Adapters and the composition root retain the SDK default. This resolves the engineering question; it does not provide Firmware/RTOS Specialist or Owner approval.

At ESP-IDF v6.1 commit `fff9895c82d744c7237be8847347bdd1b07c6643`, `tools/cmake/build.cmake` selects `-std=gnu23` for the target compiler. The older Issue #11 comment's gnu17 default is not this pinned source's behavior. `C_STANDARD 11` alone is insufficient: CMake may decay unless `C_STANDARD_REQUIRED` is set, extensions default ON, and raw SDK/compiler options can override properties.

Implementation: root `CMakeLists.txt` includes [cmake/nn2_c11.cmake](../../cmake/nn2_c11.cmake) **after** `project()`, when IDF component targets exist. `nn2_require_c11(target)` sets `C_STANDARD 11`, `C_STANDARD_REQUIRED YES`, `C_EXTENSIONS NO`, and C-only private `-std=c11 -pedantic-errors`. A forced [guard header](../../cmake/nn2_c11_guard.h) requires `__STDC_VERSION__ == 201112L` and `__STRICT_ANSI__`. A later C17 or GNU11 source override therefore causes compilation to fail. Unsupported compiler families fail closed. Root placement avoids adding commands to component CMake files rejected by PR #38's restricted parser.

The root hook uses the canonical paths and compiles one independent object probe per `.h` in each existing layer, including header-only Ports. Each probe inherits the owning component�s public/transitive requirements and private include directories and compile definitions. Repository-relative header paths are SHA-256 hashed for distinct probe filenames and target identities; `nn2_c11_header_probes.tsv` records coverage. Unrelated private headers are never required to coexist in one artificial translation unit; intentional combined-header/include-order checks remain separate review or test work. Probes are build checks and are not linked into product firmware. Header self-containment and per-header include order still require review and host tests. Future host build targets must call the same helper and compile all port headers with only inner-layer include directories, standard library and explicit mocks. Product Class U tests must execute a nonzero test count; these contract fixtures do not constitute product Class U evidence.

Verification: `tests/architecture/test_c11.py` builds CMake fixtures with GCC/Clang on host and with the pinned Xtensa compiler in CI: ISO C11 features succeed; later C17/GNU11 overrides and GNU statement expressions fail. `tools/verify_c11_idf.py` creates synthetic components outside the product tree, exercises actual IDF component registration, builds separate private-header source TUs plus header-only Ports, and checks a one-to-one header/probe inventory and the exact expected source/probe compile-command coverage with effective `-std=c11`. Helper-level regressions cover private namespace conflicts, matching include guards, component context and collision-safe identities. `tools/verify_c11_regression.py` confirms that both F1 regressions fail against the unchanged reviewed helper before checking the corrected implementation. CI retains generated probe sources, fixture headers, inventories, compile commands, logs and checksums for 90 days. Normal Class B still builds the unchanged product scaffold. No production-layer C11 conformance is claimed while layers are absent.

Limits: C11 flags/guard do not prove all code is portable, detect deliberate macro spoofing, inspect link-time symbols, or constrain arbitrary project/toolchain mutations. GNU `__extension__`, suppression pragmas, C++/assembly additions, forced includes, target/source options and compiler wrappers require manual review. Only GCC/Clang toolchains are supported by this implementation. New host platforms need explicit verification. A missing compiler is a test failure, not a silent skip.

### 3.10 Q-11 decision: complementary include/dependency verification

The Verifier owns mechanical evidence; the Architect owns dependency interpretation; Firmware owns build inputs; RTOS reviews atomic/concurrency exceptions. No layer may use a scanner blind spot as permission to depend outward.

| Mechanism | Class S guarantee / limitation | Required complementary verification and responsibility |
|---|---|---|
| Literal direct includes | Main's grep recognizes only its existing textual patterns; PR #38 adds lexical/path normalization and inward edges with fixtures. Neither is a full preprocessor. | Verifier runs the exact-head gate/fixtures; Reviewer compares supported patterns with Section 3.3. Conservative scanning can flag inactive preprocessor branches (false positives); rewrite or request a reviewed exception, never bypass CI. |
| Macro-expanded includes (G3), macro-built platform calls | No expansion guarantee; a literal scan can miss them (false negatives). | Firmware/Verifier compile all inner source and public headers on host with only permitted include roots; exercise configuration branches. Reviewer inspects macros and call targets. Disabled/unbuilt branches remain manual risk. |
| `__has_include` | May hide an outward include or select a fallback that makes an isolated host build pass. No Class S guarantee. | Reviewer inspects every conditional include/probe and supported configuration; Verifier builds both relevant branches where executable. Conditional platform dependencies in inner layers are forbidden, even if absent on host. |
| Generated headers (G5) | Scanner sees only files present when run; generation can occur later. | Generated headers in inner layers are unsupported by default. Firmware must propose an ADR exception identifying generator, reproducibility, output roots, provenance and post-generation scan/build before adoption. `sdkconfig.h` remains forbidden. |
| Transitive includes (G4) | Direct scan does not compute a transitive graph. PR #38's explicit REQUIRES audit checks declared component names only. | Verifier's future product host build must use restricted include roots and retain compiler `-M`/dependency output including system headers (`-MM` alone can omit `-isystem` headers). Class B compile commands/dependency files are reviewed against allowed roots. Link symbols/callback targets remain manual architecture review. |
| Include directories / search order | A legal header name can resolve to an unintended platform/header shadow. | Firmware records compiler, working directory, `-I`, `-isystem`, `-iquote`, forced includes, sysroot and environment (`CPATH` etc.). Verifier retains compile commands and resolved dependencies; Reviewer checks provenance/search order, system-header classification and mocks. Host isolation is complementary, not proof across target configurations. |
| Build-generated/implicit dependencies | Textual CMake audit cannot evaluate arbitrary toolchain files, target mutations, Component Manager or build graph generators. PR #38 deliberately rejects unsupported component syntax. | Reviewer audits root/toolchain/build changes and generated dependency graph. Verifier performs clean host/target builds of supported configurations and retains generated evidence; new dynamic mechanisms require a reviewed contract extension. |

**What is executed now:** existing Class S behavior is preserved apart from canonical path acquisition; manifest/detection regressions, synthetic C11 host/target checks and actual IDF fixture are automated. **What is required when product layers land:** isolated host build of all inner TUs/headers, retained full compiler dependencies and compile-command review, branch/configuration coverage and port acceptance tests. No product dependency-graph checker or host unit suite is introduced here. **Deferred/unsupported:** comprehensive preprocessor/CMake interpretation, exhaustive feature-branch expansion and automated link/callback reachability. Residual risk is explicit manual architecture review until a separately reviewed implementation closes it. Class S success alone cannot close G3/G4/G5 or prove absence of transitive/platform dependencies.

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
| Q-10 | Engineering decision resolved in 3.9; independent Firmware/RTOS review and Owner decision pending. | Firmware + RTOS Specialist / Owner |
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
