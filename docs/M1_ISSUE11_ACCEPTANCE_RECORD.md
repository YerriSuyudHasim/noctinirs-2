# Issue #11 acceptance record and N5 execution sequence

Prepared 2026-10-19 by the Integration Engineer, author-side record only. Refs
[Issue #11](https://github.com/YerriSuyudHasim/noctinirs-2/issues/11).
Base: actual #43 squash `d92796d73806e2e97092329be3741b3ddac19c54`, complete tree
`6fb344225d6ea7a489e7eaf5b18d0b5bebf3b88c`. This document does not close the issue,
approve its own revision, or authorize any merge/settings/hardware action.

## Issue #11 acceptance-criteria matrix

The six rows below correspond to the actual issue's criteria, not its historical
unchecked boxes. Earlier checkpoints and decision packets remain historical records.

| Issue criterion | Evidence already merged / recorded | Evidence supplied by this draft PR | Actual remaining gap |
|---|---|---|---|
| Q-10 resolved with rationale and verifiable enforcement point | ADR 3.9; root CMake hook, strict compiler guard, corrected independent per-header probes; #39 then #43 merged. Non-author review closed F1. | Record existing bounded acceptance; verify status, provenance and retained obligations in architecture tests. No compiler/helper change. | No unresolved Q-10 design/enforcement criterion established. Review this record revision. Future real product compiler coverage remains an implementation obligation. |
| Q-11 detection strategy and documented residual risk | ADR 3.10 complementary scan/compiler/manual strategy; G3/G4/G5 and root/toolchain/header-shadow/link limitations explicit. | Preserve limitations and distinguish static acceptance from future product verification. | No unfinished strategy criterion. Full preprocessing/transitive proof was not promised; accepted residual limitations are not hidden completed implementation. |
| N6 one machine-readable layer path list consumed by ADR/workflow | `tools/layers.json` schema v2 and validated consumers, detector/scanner/wrapper/CMake/fixture; full #43 migration merged after partial #39. | No policy-map or workflow change; retain canonical-reference regression. | No remaining canonical migration identified. Manifest/schema changes still need architecture review. |
| Port contract template committed/referenced | `docs/templates/port-interface-contract.md`, ADR 3.6: ownership/lifetime, errors, timing/blocking/ISR/thread annotations and lifecycle. | Preserve template and implementation-specific obligations. | No template criterion gap. Concrete per-port contracts and implementation evidence belong to subsequent component work. |
| Independent non-author review recorded | [#39 approval](https://github.com/YerriSuyudHasim/noctinirs-2/pull/39#issuecomment-6247561760), [#43 approval](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6247562819), [reconciliation confirmation](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6256423199). | Bounded IR handoff for the new documentation/test diff and its exact final SHA. | **Non-author review of this new revision is pending.** Earlier implementation approvals do not approve this diff. Relay records are not native GitHub approvals. |
| Owner summary and Accepted status only by Owner decision | [Actual acceptance, 2026-10-18 09:22:14 UTC](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6248188579); [later integration authorization](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6256494704) left source Proposed until separate reviewed change. | ADR Section 0, Accepted metadata, date, accepted tree/source links, rev 4 and tests. Historical Proposed revisions retained. | **This draft's review and separate Owner merge authorization remain pending.** Main stays Proposed until this reviewed change is merged. Issue closeout is a separate Owner action. |

Evidence: [bounded Firmware/RTOS consultation](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6247910114)
recommended ACCEPT without static-framework amendments; one non-author assessor held
all three assigned roles, not three independent reviewers. [#43 final execution](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43#issuecomment-6256587588)
binds merged source to successful fresh post-merge runs
[39176824755](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39176824755)
and [39176824705](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39176824705).
Those are prior implementation evidence, not CI for this new revision. The PR handoff
must identify this revision's exact final SHA and fresh CI separately.

R1/R2 retain their existing conditional scope, F1 remains CLOSED, and no new exception
is introduced. Product layers remain absent at this base: production Class S is
SKIPPED / NOT APPLICABLE, never conformance PASS. Scanner, architecture and compiler
fixtures verify tools; they do not establish product Class U, runtime, hardware,
timing, electrical or clinical behavior. Those future obligations are not extra
unfinished Issue #11 design criteria.

## N5: concrete sequence, prepared only

Authority: [approved N5 exception](https://github.com/YerriSuyudHasim/ai-engineering-hub/issues/2#issuecomment-6236882761)
and [execution/restoration boundary](https://github.com/YerriSuyudHasim/ai-engineering-hub/issues/2#issuecomment-6236974582).
Restore meaningful real `class-u-host-tests`, integration **15368**, before the first
production `nn2_*` component/runtime merge (including Issue #13 / PR #15), or
**2026-10-25T01:00:00Z**, whichever is earlier. No available job or dependency delay
extends that deadline. At expiry, restore absent a new explicit Owner-approved
extension, even if this blocks merges. No settings action is performed by this PR.

### Live starting points inspected

- [Issue #13](https://github.com/YerriSuyudHasim/noctinirs-2/issues/13) is OPEN: first
  pure Core component, meaningful normal/boundary/error unit behavior, host C11 and
  zero-warning IDF builds, Class S, reference integrity, Reviewer/Verifier review;
  framework provenance/license decision remains an explicit dependency.
- Existing [draft PR #15](https://github.com/YerriSuyudHasim/noctinirs-2/pull/15) is at
  `6cf7e82c5969fff993ad5fcc61b5a97c9d1fc739`, branch `feat/m1-components-draft`.
  It already contains Core ring/status, Ports, App sequencer, fake/disabled adapters
  and `test/host`, plus a real `class-u-host-tests` job. It is broader than a Core-only
  slice: the Architect/Owner must confirm its intended #13 scope or agree a bounded
  split of existing work. Do not silently adopt all runtime work or build a duplicate.
- [Draft PR #40](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40) is at
  `f97767035e0ab4f801e655ec751141314668c407`, branch `ci/9-reproducible-build`.
  Its [independent review](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40#issuecomment-6237139654)
  requests changes: F1 unreadable nested inventory can be omitted; F2 warning-log
  read errors can pass the new zero-warning guard. Successful old clean builds do
  not close these failure-path defects. Its single reproducible-build configuration
  proposal is separate from approval of defective evidence guards. No #40 changes
  are imported here.

### Execution gates for separately authorized follow-up

1. **Resolve reproducibility first (Issue #9 / existing #40).** Its author should
   remediate F1/F2 with negative/error regressions, then refresh against actual current
   main without losing #38/#39/#43's Python scanner, canonical policy, C11 helper or
   new tests. Reconcile shared workflows deliberately. Obtain exact combined-head
   two-clean-different-path raw binary evidence, zero-warning Class B, reference and
   gate/architecture CI; obtain non-author re-review and explicit Owner merge approval.
   Only that later authorization can merge it. Old #40 CI on its obsolete base is
   not combined-tree evidence. Issue #13's reproducible-build dependency remains open
   until resolved or separately decided by the Owner; ADR acceptance does not waive it.
2. **Refresh existing #15 under explicit branch-work scope.** After dependencies
   are resolved, merge actual current main into its existing branch using an isolated
   worktree, preserving published history. Do not wholesale replace the current
   workflow with #15's old grep-era workflow; retain schema v2, fail-closed scanner,
   independent per-header C11 enforcement and reproducibility guards. Its historical
   instruction to rebase onto old #18 is stale: the actual hardened gate landed via
   #38 and canonical integration via #39/#43. Review against those actual squashes.
3. **Verify product contract and behavior, not fixture counts.** Preserve/review the
   existing ring tests for empty pop, invalid arguments, full capacity/drop accounting,
   FIFO order and repeated wraparound, and status behavior. Expand assertions where
   needed for invalid-input nonmutation and boundary semantics. Review the sequencer
   tests for port validation, exact call order/sample contents, ADC/on/off failure
   paths, failed-off state/call counts, reset failure staying FAULT, successful reset
   clearing last_error, ring-full drop behavior and FAKE versus zero-init UNKNOWN
   provenance. Confirm no further on() after an unrecovered fault in the composition
   root. These exercise actual production C functions with explicit mock contracts;
   a no-op binary or scanner fixture cannot replace them. Old 16-test reports are
   historical, not results at the refreshed candidate.
4. **Close component-specific design/review gaps.** Refresh Architect/Verifier review
   against the accepted contract, own/borrow/lifetime/error contracts and dependency
   limits. Obtain RTOS disposition for the still-present up-to-1-ms busy-wait and
   task/watchdog/fault behavior (#12); static ADR consultation does not approve it.
   Confirm test-harness provenance/license disposition (Issue #13 / Q-7) even if using
   the existing custom harness rather than Unity. Keep ADC fake/emitter disabled;
   parked real adapters remain excluded. Hardware deferrals in #19 remain, with no
   hardware test or real-ADC measurement claim.
5. **Run and retain real Class U at exact candidate SHA before restoring the rule.**
   Native host configure/build uses C11, extensions off, `-Wall -Wextra -Werror
   -pedantic`, restricted inner include roots and no SDK stubs concealing dependency
   violations. Build/test all applicable actual inner source units and independent
   headers; retain full compiler dependency output (including system headers),
   compile commands, compiler versions, test enumeration/results and hashes. Use
   `cmake -S test/host -B <fresh-dir>`, `cmake --build <fresh-dir>`, then
   `ctest --test-dir <fresh-dir> --output-on-failure --no-tests=error` and verify
   nonzero product-test execution and nonzero failure propagation. The job must run
   on every applicable candidate without path/conditional skips or synthetic status.
   Obtain real-layer Class S success/invariant (no absent-layer skip), reference,
   architecture/C11, reproducibility and zero-warning target Class B on the exact
   reconciled SHA; independent Reviewer/Verifier assess logs and behavior coverage.
6. **Restore before any production merge, not after #15 lands.** Under a separate
   explicit Owner-authorized settings execution, re-read full/effective rules and
   reinsert only `{"context":"class-u-host-tests","integration_id":15368}` into
   then-current required checks, preserving all unrelated properties. Verify full
   ruleset and effective-main readbacks and exact-candidate real check provenance.
   Record the restoration in governance Issue #2 and product records. The real job
   may run on the unmerged #15 branch before the requirement is restored; it need
   not first be merged into main. If this makes tooling-only PRs wait for a real
   job, that is a block to plan around, not permission for an empty substitute.
7. **Only then request a fresh Owner merge decision for #15's final scope/SHA.**
   Revalidate head/base/checks/reviews/threads/protections after restoration. Any new
   source or base reconciliation needs applicable CI/review. No merge or issue
   closeout follows automatically from N5 restoration or this ADR record.

If #40/#15 cannot be ready by expiry, the concrete fallback is restoration of the
requirement at expiry (or a new explicit Owner-approved governance extension), with
production integration blocked. This document creates neither an extension nor an
automation and changes no settings or component implementation.

## Bounded independent-review request

Review the ADR decision date, exact accepted tree, authoritative links and recorded
scope against the actual Owner comments. Confirm historical Proposed records remain,
R1/R2/F1/N5 and production evidence semantics are preserved, and tests validate the
current status rather than merely finding Accepted/Proposed somewhere in history.
Check this matrix against Issue #11 and the N5 dependency/test plan against the live
#13/#15/#40 records. Inspect exact-final-HEAD CI. This is a new documentation/test
review, not a request to repeat unchanged implementation builds independently, approve
runtime code, merge, change protections or close issues.
