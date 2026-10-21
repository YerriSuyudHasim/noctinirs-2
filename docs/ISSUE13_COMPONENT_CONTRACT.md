# Issue #13 / PR #15 component contract, revision 2

| Field | Value |
|---|---|
| Identity | Issue #13; ADR-0001 §§3.4/3.10; Core/Ports/Application headers under `components/nn2_*`; exact evidence SHA is recorded by CI and the unpublished author handoff |
| Accountable owner | Assigned Firmware Developer / implementation author; Architect owns boundary assessment |
| Reviewers | Separate non-author Reviewer and Verifier; bounded Architect and RTOS assessments pending; Hardware delta confirmation limited to changed documentation/claims |
| Status | Proposed candidate contract, not independently approved; Owner scope/Unity decision: [record](https://github.com/YerriSuyudHasim/noctinirs-2/issues/8#issuecomment-6208383677) |

## API purpose and boundaries

The Issue #13 pure Core slice is the fixed-capacity 16-element FIFO and internal
sample/status/provenance types. Supporting Ports and the synchronous Application
sequencer exercise one small use case. The sample is an internal C value contract,
not a packed record or stream/wire protocol. It has no physiological meaning.
No historical firmware behavior or code is adopted. The canonical schema-v2
manifest is unchanged: Core has no component dependency; Ports depend on Core;
Application depends on Core/Ports. `main/` alone binds concrete adapters.

## Opaque caller storage and C11 object model (A1 remediation)

`nn2_core_ring_t` and `nn2_app_sequencer_t` are forward-declared incomplete
operational handle types. Public headers expose no operational struct definition.
Sample/config/port tables and copied observation records remain distinct value types.
Caller declarations use `nn2_core_ring_storage_t` (1024 bytes) and
`nn2_app_seq_storage_t` (512 bytes), with `max_align_t` union alignment; these are
inert representation storage, not a stable internal-layout ABI. The bound is checked
against actual private state size by `_Static_assert` on each supported compiler.
No internal struct is placed into or dereferenced through that byte array.

The storage object has its ordinary declared C type/lifetime. Init validates all
arguments, initializes a private automatic **typed** state object, clears storage
bytes and copies its representation into the unsigned-char array with `memcpy`.
A handle is an alignment-checked identity pointer converted from the storage object
address. It is **never dereferenced as the opaque tag struct**: every access first
converts back to the original storage pointer type (C11 6.3.2.3), then uses the
actual unsigned-char member. Each operation loads the saved representation into a
new local object of the declared private state type, operates on that object and
copies it back. There is no byte-array-to-state-struct lvalue cast, union type-punning,
placement/lifetime extension, pointer retained into an expired automatic state,
heap, or change to declared effective type (C11 6.5 / memcpy object representations).
Padding is never interpreted as policy or compared as sample-field equality.

Caller owns storage, handle and lifecycle. Static, automatic and embedded/array
storage are supported; moving/copying/inspecting/modifying live storage is forbidden.
Exactly one exclusive owner may use any aliases of a handle. Init must succeed
before any handle operation. Zero-filled storage by itself is not initialized.
Storage must outlive all handles, the attached sequencer and all borrowed contexts.
Outputs/config/sample/storage must be disjoint. No constructor-side resource is
acquired, so no destructor/free API is necessary. To end a sequencer lifetime,
quiesce all calls, obtain any required successful reset/off result, invalidate every
alias, then allow its storage lifetime to end; off status is not physical proof.
End sequencer borrowing before ending/reinitializing its ring. Reinit is allowed
only after the old session is quiescent and prior aliases have been invalidated;
it resets counters/config but does not clear the independent ring. Failed init
leaves both storage and the output handle unchanged. Stale or corrupted handles,
reentrant callbacks and access during lifetime transitions are invalid preconditions,
not runtime-detected errors. There is no raw-buffer alignment assertion by fiat.

Public observations are copies: ring count/full/dropped and sequencer
`nn2_app_sequencer_observe` state/counters/last_error. Modifying an observation
cannot modify the module. Private headers under `src/` are included by implementation
and explicit white-box tests only; no public include or composition-root caller
imports them. White-box snapshots preserve head/tail/content nonmutation assertions;
only rollover preconditioning uses a private save seam. Normal behavior calls the
production API. Public-only negative compiler probes reject direct ring/sequencer
field access. All public/private inner headers are independently compiled.

Cost/limits: bounded representation copies add per-call stack/copy work (a whole
private ring snapshot, or sequencer snapshot, with nested ring operations). This
trades that cost for a straightforward defined C11 model. No zero-copy, measured
latency, stack margin or watchdog-safety claim follows. Fresh bounded Architect and
RTOS delta review must assess actual compiler sizes/alignment, copying, aliasing,
call graph and stack implications. This author does not close A1 independently.

## A2 decision record and unchanged exclusions

The [actual Owner decision](https://github.com/YerriSuyudHasim/noctinirs-2/pull/15#issuecomment-6306981230) was relayed once under the active
Human Owner instruction, after verifying proposal file SHA-256
`7afeb0ddea5ba104a4bd3cfb496c01bd692284a06f764207669bbb50f8995477`.
Its exact decision text/marker and relay attribution were separately read back;
GitHub user ID 342860889 / OWNER matches the registered Owner. It grants only the
pinned standard-library implementation-chain clarification now recorded in
[ADR §§3.3/3.10](adr/0001-clean-architecture-boundaries.md#bounded-a2-owner-decision-pinned-standard-library-implementation-chain).
The source record and remediation still need exact-candidate non-author review.
The decision is not source approval, independent approval or merge/settings authority.

For this slice and ESP32-S3 IDF v6.1 `fff9895c82d744c7237be8847347bdd1b07c6643`,
Xtensa GCC `esp-15.2.0_20251204`, pinned image
`espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`,
permitted standard headers (assessed chain includes stdint) may reach the unmodified
esp_libc sys/cdefs shim and generated sdkconfig solely for libc configuration.
Shim SHA-256 remains `45151a90d09f8e7d6b8acab050f2e15686e7fdca70d29b298df88af16d4d884a`.
Product-owned SDK includes/CONFIG_* references/conditional fallbacks/framework or
RTOS/driver APIs/generated inner headers remain prohibited. No fake/shadow headers,
SDK modification, hidden dependencies, weakened controls or warning suppression.
SDK-free host isolation, strict C11, independent probes and complete host/target
commands/dependencies/search roots/environment/config evidence remain mandatory;
broad target roots are disclosed. Changed SDK/toolchain/config/shim or additional
resolved platform headers require fresh Architect/Verifier assessment and any
policy-required decision. Future modules and other dependencies are not covered.
R1/R2 and N5/deadline stay unchanged; no runtime/timing/hardware/safety/clinical
approval or other restricted action follows. Prior exact-source graph evidence is
[retained here](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39531735121/artifacts/12315877257);
fresh changed-candidate graph evidence belongs in the new author handoff.

## Ownership, allocation and lifetime

| Object / parameter | Owner / allocator | Borrow or transfer | Valid until | Cleanup and failure behavior |
|---|---|---|---|---|
| Ring and embedded 16 samples | Caller; typed static/automatic/embedded aligned storage and opaque handle | No transfer; push/pop copy the entire sample value | Exclusive owner ends use; must outlive attached sequencer | No allocation/free/deinit; init/reinit discards all records and counters |
| Sequencer | Caller; typed aligned storage and opaque handle, no heap | No transfer | Owner stops calls and quiesces callbacks before lifetime ends | No destructor; reset requests off; failed reset retains FAULT |
| Config input | Caller | Copied by successful init, including port values; not retained by address | Config may expire after init returns | Missing required input returns INVALID_ARG before changing sequencer; no callback/resource acquisition |
| Ring pointer and each `void *ctx` | Caller or adapter | Borrowed by copied config; callbacks never take ownership | Through last step/reset/callback, including cleanup | Caller releases resources only after quiescence; null context valid when callback implementation supports it |
| Push sample input | Caller | Borrowed for call; value copied into ring | Return | Unchanged, including on FULL/INVALID_ARG |
| Pop output | Caller | Copy into caller storage on success | Caller owns resulting value | Unchanged on EMPTY/INVALID_ARG |
| ADC `raw_out` | Caller (sequencer stack) | Borrowed for synchronous callback only; one uint32 raw count | Callback return | Written only on OK; otherwise unchanged by conforming adapter; sequencer ignores failed output |
| Log message | Application/static string | Borrowed, non-null, NUL-terminated, for synchronous call only | Callback return | Logger must copy if retained; void return means failures are not propagated |

No DMA, asynchronous request, queued payload, cancellation callback, registration,
allocation or acquire/release pair exists. Cleanup is synchronous. Ring/sample/out,
sequencer/config/ring storage must not overlap; in particular, config may not alias
sequencer storage during init, and push/pop arguments may not alias ring storage. Each
object must have the proper C type, alignment and lifetime. Concurrent or callback
reentrant use is forbidden. Init is required before any non-null stateful operation;
arbitrary corrupted/uninitialized storage or stale handles are outside the valid-input domain.

## Operations and error propagation

| Operation | Preconditions / success postconditions | Status | Output on failure / retry |
|---|---|---|---|
| Ring init | Non-null typed storage/output-handle; sets private state and output handle on success | OK / INVALID_ARG | Storage and *out unchanged on error; quiescent reinit discards contents and invalidates old aliases |
| Ring push | Initialized ring and non-null sample; copies at head then advances modulo 16 | OK / FULL / INVALID_ARG | On FULL only dropped increments; head/tail/count/content unchanged; no overwrite; retry after pop |
| Ring pop | Initialized ring and non-null output; FIFO copy then tail advances modulo 16 | OK / EMPTY / INVALID_ARG | Ring and output unchanged on failure; retry after push |
| Ring queries | Initialized non-null ring, or null | Count/full/drop observations | Null count=0, full=false, dropped=0; queries never mutate |
| Sequencer init | Non-null storage/output/config/ring; on/off/read/now required | OK / INVALID_ARG | Validation before mutation; no callbacks; resets sequencer counters, not ring |
| Observe | Initialized seq, non-null disjoint output | OK / INVALID_ARG | Copied state/counters/last_error; out unchanged on error |
| Step | Initialized seq; one state transition | OK / callback primary status / INVALID_ARG / INVALID_STATE | FAULT/invalid state do not call ports or mutate; failed cycle never publishes |
| Run cycle | IDLE only; synchronous transitions until IDLE or FAULT | Same as step | Non-IDLE returns INVALID_STATE without calls; null returns INVALID_ARG |
| Reset | Initialized seq; allowed in any valid state | OK / off status / INVALID_ARG | Calls off once; failure increments off_failures, sets last_error, retains FAULT; success IDLE/last_error=OK; counters and published ring remain |
| Name queries | Any enum value | Static non-null string | All status values supported; status sentinel/invalid -> UNKNOWN; source invalid -> INVALID; state invalid -> UNKNOWN; null state query -> FAULT |

The supported statuses are OK, INVALID_ARG, INVALID_STATE, FULL, EMPTY, IO,
NOT_SUPPORTED and TIMEOUT. `NN2_STATUS_COUNT_` is a sentinel, not a result.
Ports translate platform failures to Core statuses; callers never infer `errno`.
These fake/disabled adapters have no platform error mapping or hardware resource
acquisition. A future real adapter must specify mapping/retained raw diagnostics
under #19. For this use case any callback error is session-fatal; Application
requests off and enters FAULT. No system-fatal restart, abort or watchdog recovery
policy is implemented.

`uint32_t` sequence/cycle/fault/off-failure/drop counters wrap modulo 2^32; they are
not lifetime-unique identifiers or saturating safety counters. Sequence advances
after successful normal off, including full-ring drops. `cycles_ok` counts completed
acquisition attempts including drops, not publications. `faults` counts first error
transitions; repeated FAULT calls/reset failures do not increment it. `off_failures`
counts failed cleanup/reset calls, excluding the initial failed normal off.

## Initialization, deinitialization and recovery

Uninitialized -> init -> IDLE -> on + timestamp -> LED_ON -> optional delay ->
SAMPLE -> read + timestamp -> LED_OFF -> off -> copy/drop -> IDLE.
On/read/off error -> cleanup off -> FAULT. An initial off failure causes exactly
one cleanup retry; even a successful retry retains FAULT and publishes nothing.
Cleanup failure is counted/logged while the primary operation error is returned
and stored. Reset failure returns/stores its own off error, stays FAULT, and
increments off_failures. Only successful reset returns IDLE. FAULT step/run_cycle
cannot issue another on. Recovery starts a fresh pending record on the next IDLE
transition; failed pending records never enter the ring. Pending fields are internal
and invalid to consume until publication. Quiescent reinit is permitted only after
the caller has already ended the old session and obtained any required off result;
init itself performs no off and must never be used to bypass FAULT cleanup.

An injected off success is only a returned status. It proves no physical safe
state, independent cut-off, optical limit or shutdown latency. #19 retains real
emitter default-off/cut-off and hardware-contract obligations. There is no
asynchronous cleanup to cancel/drain; published samples may be drained separately.

## Blocking, timing and ISR safety

| Function / callback | Context | Blocking / requirement | Evidence |
|---|---|---|---|
| Ring init/push/pop/queries; enum names | One owner task, never ISR for this slice | Bounded local work; no waits, locks or callbacks | Host logic only; no target latency claim |
| Sequencer init/state/name | Owner task, no reentry | Local work | Host/static |
| Step/run_cycle/reset | Owner task, no reentry | Synchronous; bounded only if injected callbacks meet contract; no infinite retry | Host/static; RTOS assessment pending |
| on/off | Same caller task | Nonblocking requirement; prior <50 us expectation remains unmeasured | H NOT RUN |
| ADC read | Same caller task | One synchronous conversion/result; prior <100 us expectation unmeasured | Fake adapter only; H NOT RUN |
| now_us | Same caller task | Nonblocking, monotonic uint64 microseconds; common origin for both timestamps | Model only; resolution/cross-core/wrap H NOT RUN |
| delay_us | Same caller task | Optional void callback; zero settle skips it; no error/timeout return | Host model only; target busy wait 0..1000 us Kconfig |
| log write | Same caller task | Optional, synchronous, non-reentrant; finite completion required | No deadline guarantee; H NOT RUN |

No callback may reenter a sequencer/ring or change config/state while an operation
is active. Different objects may be used independently only with disjoint mutable
contexts/rings. No cross-core sharing, locks, atomics, tasks or ISR synchronization
is added. Maximum one in-flight operation per object; external serialization is
required. There is no lock order or atomic memory order to approve here.

Timestamps are taken **after** successful on and **after** successful read, not at
physical switching or conversion edges. No timeout is inferred from their difference.
The uint64 timebase horizon/wrap handling is not a scheduler; wrap/rollback is an
adapter-contract limit. delay/log have no status result, so a settle/log failure
cannot enter seq_fault. A missing delay means no settle wait even if settle_us>0.
No clock-delay error return or production fail-safe is introduced to hide that limit.
`main` uses the ESP-IDF app_main caller task, bounded Kconfig busy wait and a
100 ms tick-converted inter-cycle task delay; no new task/ISR. Watchdog, tick
rounding, scheduling and actual timing remain specialist/target review obligations.

## Host mock and ESP-IDF expectations

Host tests link the production three C translation units and deterministic injected
ports, with only approved inner include roots, standard library and explicit Unity
test dependency. All nine public/private headers compile independently and in two intentional
include-order probes through the shared C11 helper. Normal/error/boundary, cleanup,
optional callback and null-context cases execute. Busy/disconnect/cancel/queued
completion are N/A to this synchronous API. Settle failure is unrepresentable.
The ESP fake adapter's `.source=FAKE` assignment remains **N7 static-only** evidence;
host mock execution does not execute that ESP adapter. Target compilation is Class B.

## Verification and acceptance

| Criterion | Procedure / owner | State before exact-source CI/review |
|---|---|---|
| Issue #13 Core host + target zero warnings | Shared C11 host build; pinned IDF build; strict raw-log validator / Verifier | Fresh CI required |
| Issue #13 normal/boundary/error coverage | 25 retained behavioral intentions adapted to opaque APIs; five added storage/observation cases / Verifier | Fresh CI required |
| Issue #13 real Class S + reference integrity | Canonical detector/scanner/invariant; exact-source reference workflow / Verifier | Fresh CI required |
| Issue #13 independent Reviewer + Verifier | Non-author review of final source and obtainable evidence | Pending, author cannot supply |
| Issue #13 no hardware claims | Scope and evidence classification / Reviewer | H NOT RUN; no hardware execution authorized |
| Supporting Ports/Application | Contracts, cleanup/error/FIFO/field tests; isolated failure and zero-case controls | Fresh CI and review required |
| Representation/dependency/build assessment | Architect: opaque memcpy/storage object model and A2 decision-record fidelity; full dependencies, macros, links/callbacks | Explicit assessment pending |
| RTOS/Hardware | Single owner, caller task, busy wait, inter-cycle delay, watchdog/FAULT; bounded hardware claim delta | Pending; old review retains original SHA scope |

## Evidence, risks and recovery

CI records full source SHA, UTC, tool/environment, actual compile commands/working
directories, `-M` dependencies including system headers, search roots and include
environment. Artifacts retain at least 90 days with SHA-256 inventories and archive
metadata; controlled preservation before expiry remains an obligation. Local logs
are diagnostic, not durable final evidence. Existing two-clean-build procedure must
pass on this exact candidate with all raw binaries equal; no normalization.

Manual Q-11 assessment for these files: inner includes are literal; conditionals are
include guards and C++ linkage guards only; no `__has_include`, generated inner
headers, SDK fallback, atomics, suppression pragmas, custom compiler wrapper or
source dialect override. Core uses only stdint/stdbool/stddef/string. No outward
function symbol appears in Core; Application invokes only copied function pointers
and Core functions. Host mocks and composition-root bindings require separate
review; runtime substitution and unbuilt target/configuration branches remain
unverified. Complete fresh target dependencies and A2 bounded-chain fidelity still need non-author inspection.
No complete preprocessor/link reachability certification is claimed. R1/R2 and
#39 F1 / #40 F1/F2 scoped closures remain unchanged.

Rollback before merge: review/revert only this feature branch's candidate changes;
preserve published history. No device/partition/release change exists to recover.
N5 settings restoration is separate: meaningful Class U plus non-author verification,
explicit settings authority and full/effective readback are required before first
production merge or 2026-10-25T01:00:00Z, whichever is earlier. Missing tests never
extend expiry. This task changes no settings and creates no automation.
