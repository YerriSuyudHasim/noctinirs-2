# PR #39 integration prerequisite refresh

Integration author: Codex, 2026-10-18 UTC. This is author-side integration,
not independent approval. Intended order: #39, then #43; no main merge.

## Inputs and resolutions

- Current main / merged #38: `cc2066db6cd01bb2bfa72e304d530cab2963cc22`.
- Corrected and F1-reviewed #39: `128755a5c25ff95b30ae99c2205613aa028f72ac`.
- Separate stale #43: `e65e6596b32d9fdc6d779cddfd6fa583f3031efc`.
- Common base: `4a21171353b72ceff62774ba114705e0c8be2c31`.

Live remote refs were checked before worktree mutation. Work uses a detached,
isolated worktree; published histories are preserved by a two-parent merge.
The resulting commit is identified in the append-only PR handoff.

Workflow conflicts: retain #39's validated canonical detector, independent-header
C11/architecture jobs and artifact retention; retain main's Python gate and all
Issue #10 regressions. Wrapper conflict: retire grep, validate the canonical
manifest before invoking the unchanged main scanner. Selftest conflict: retain
main's component registrations/permission handling; replace obsolete grep-error
injection with interpreter failure and preserve Python traversal-error injection.
No whole workflow was selected as a conflict resolution.

The main scanner and its 59 behavioral expectations remain. Detector harness
setup now copies its real validator/manifest and selects the actual test Python
on Windows; expected gate/detection verdicts are unchanged. The wrapper fixture
copies the real scanner and checks its equivalent empty-layer diagnostic.
Additional tests execute the actual detector on five cases and the invariant on
all 48 states. CI retains gate logs as well as generated C11 probes, fixture
headers, inventories, commands, logs and hashes.

## Current scope

**N6 remains PARTIAL on #39.** Its detector, CMake hook and wrapper validate the
canonical path manifest; the Python scanner still owns its original independent
path/dependency/include/extension policy. Full migration belongs exclusively to
#43. No #43 implementation was imported here. F1's corrected per-header helper,
private context, header-only Ports, guard and collision-safe inventories remain.

Current-state guidance here supersedes older integration-status prose in the
historical remediation/decision records. Final exact-SHA test counts, CI URLs,
artifact hashes and any blockers belong to the append-only PR handoff. Earlier
source CI is not evidence for this tree. Initial local harness failures remain
in local logs; no failed run is counted as success.

ADR-0001 stays **Proposed**. R1/R2, unsupported CMake fail-closed behavior and
manual/deferred macro/transitive/generated/root/toolchain/link/standard-header
review remain. stdatomic requires RTOS consultation. Production Class S is
SKIPPED / **NOT APPLICABLE**, not conformance. Product Class U and hardware/HIL
are NOT RUN. Firmware/RTOS agreement, non-author review and Owner
Accept/Reject/Amend remain pending.

The approved [N5 restoration obligation](https://github.com/YerriSuyudHasim/ai-engineering-hub/issues/2#issuecomment-6236882761)
requires meaningful real class-u-host-tests bound to Actions integration 15368
before the first production nn2_* component/runtime merge (including #13), or
2026-10-25T01:00:00Z, whichever is earlier. Restore at expiry absent a new
explicit Owner-approved extension. Synthetic/empty/skipped success cannot substitute.

No #40 changes, references, settings, issues, draft states, releases or hardware
were changed. Rollback is a reviewed revert; preserve a coherent policy/consumer
set and rerun verification. The integration author cannot approve this result.
