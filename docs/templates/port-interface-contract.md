# Port interface contract: <name>

Copy this template into the component contract before implementing a port. Fill every field with a value, requirement, or justified N/A. TBD prevents acceptance of that requirement. Keep API annotations in the header consistent with this document.

| Field | Required content |
|---|---|
| Identity | Issue, ADR, contract revision, header paths, exact evidence SHA |
| Accountable owner | Architect/Application owner and implementation maintainers |
| Reviewers | Independent Reviewer; RTOS for concurrency/timing; Hardware where applicable |
| Status | Proposed / reviewed / Owner decision link; never self-approved |

## API purpose and boundaries

Describe the use case, input/output units, ranges and invariants. Name policy owned by Core/Application and platform detail owned by the Adapter. Use C11/Core types, function pointers and `void *ctx`; ESP-IDF/FreeRTOS types and hardware constants do not cross the interface. List dependencies, callbacks and composition-root bindings.

## Ownership, allocation and lifetime

| Object / parameter | Owner / allocator | Borrow or transfer | Valid until | Cleanup and failure behavior |
|---|---|---|---|---|
| Opaque handle / context | <explicit> | <explicit> | <init to deinit; stale handle rule> | <who releases exactly once> |
| Input buffer | <caller> | <borrowed for call by default> | <return or named completion> | <unchanged/consumed on error> |
| Output buffer / returned view | <explicit> | <copy/borrow/transfer> | <next call/ack/release> | <partial result validity> |
| Callback payload / context | <explicit> | <explicit> | <including queued callback lifetime> | <cancel/drain before release> |

State capacity, alignment, nullability, aliasing, length units, maximum size, mutability and adapter-owned DMA copying requirements. List acquire/release pairs and who frees at every failure point. Do not retain borrowed pointers without an explicit lifetime agreement. Core/Application allocate no heap; any Adapter init allocation must be bounded and documented; no allocation after init.

## Operations and error propagation

| Operation | Preconditions / success postconditions | `nn2_status_t` results | Outputs valid on failure? | Retry / idempotency |
|---|---|---|---|---|
| init / configure | <state/resources/validation> | <named statuses> | <explicit> | <explicit> |
| read / write / start / stop | <relevant operations only> | <busy/timeout/invalid/fault> | <partial count or unchanged> | <bounded policy in Application> |
| cancel / deinit / release | <drain rule> | <cleanup failures> | <explicit> | <double-call behavior> |

Return explicit status codes and values through output parameters. **No hidden global errno assumption:** callers never infer a result from `errno`; implementations capture/translate platform errors before returning. Map each ESP-IDF error to a Core status and retain raw diagnostics behind the Adapter/log boundary. Distinguish transient, session-fatal and system-fatal failures; Application owns recovery policy.

## Initialization, deinitialization and failure recovery

Provide a state diagram/table: uninitialized -> configured -> running -> stopped -> deinitialized, plus fault state. Define invalid transitions, repeated operations, partial-init rollback, acquisition order and reverse cleanup. Specify callback deregistration, outstanding-request cancellation, queue draining and cleanup failure behavior. Name who requests safe state and who implements it. Hardware safe-state timing needs a separate approved measurement procedure.

## Blocking, timing and ISR safety

Complete one row per function and callback. Label unmeasured bounds as requirements, never established guarantees.

| Function / callback | Task / ISR allowed? | Blocks? | Timeout units / clock | Worst-case requirement | Evidence |
|---|---|---|---|---|---|
| <name> | <ISR forbidden by default> | <nonblocking/bounded> | <zero/infinite/wrap semantics> | <latency/jitter bound> | <Class H issue or N/A> |

Specify interrupt priority constraints, callback dispatch context and whether callbacks may reenter the port. Define backpressure, overflow/drop accounting, cancellation latency, timestamp origin/monotonicity, deadline failures and scheduling assumptions. Use approved requirements; do not import historical timing or hardware values.

## Thread safety, reentrancy and concurrency

State permitted callers, maximum in-flight requests, SPSC assumptions, ownership across cores and required external serialization. State reentrancy and safety on the same versus different handles for every operation. Document lock order, callbacks under locks, atomic memory ordering and deadlock avoidance. No implicit global mutable state. RTOS primitives stay in Adapters; Application synchronization needs RTOS review.

## Host mock and ESP-IDF implementation expectations

Host fakes implement identical signatures, statuses, ownership and lifecycle with deterministic failure injection and no IDF include paths. Test busy, timeout, partial output, disconnect, callback-after-cancel prevention and every init acquisition failure. Simulated time verifies policy only. ESP-IDF Adapters translate platform types/errors, own platform resources and provide measured evidence for ISR/timing/concurrency claims on the recorded board. Compile every port header under ISO C11 using the shared helper, both individually and in combined/include-order probes.

## Verification and acceptance criteria

| Criterion | Procedure | Evidence / responsible role |
|---|---|---|
| Boundary / C11 | Class S plus isolated host build, header probes, target build | Exact SHA, commands/compiler/CI URLs; Verifier |
| Ownership / cleanup | Positive, invalid-argument, cancel and each acquisition-failure test | Nonzero executed test count; Verifier |
| Lifecycle / errors | State transition and Adapter mapping tests, partial failure | Host and Adapter results |
| Concurrency / ISR | Review and appropriate race/stress/target tests | RTOS sign-off; unrun tests explicit |
| Timing / hardware | Approved Class H procedure on named board/fixture | Measurement or NOT RUN; Class B is insufficient |
| Independent review / decision | Review by non-author; Owner decision where required | Recorded links |

Record environment, UTC time, evidence retention, assumptions, residual risks, rollback and resource recovery. All applicable rows must pass; list blocked rows explicitly. This template approves no hardware parameters or production behavior.
