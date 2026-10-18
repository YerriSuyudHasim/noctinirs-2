# Issue #11 / PR #39: F1 remediation and integration boundary

This is implementation-author evidence, not independent approval. The independent
[REQUEST CHANGES report](https://github.com/YerriSuyudHasim/noctinirs-2/pull/39#issuecomment-6243611471)
reviewed `789b4fa8bb2904a85116c6c6116973c348565c05`. Its missing local CMake was
an environment limitation, not a functional defect. The published report was
sufficient to reproduce F1 without its reviewer ZIP.

## Correction and regression-first verification

The aggregate probe rejected otherwise valid private namespaces from separate
source translation units and could suppress an unreferenced non-C11 header with
a matching include guard. The new helper creates a fresh translation unit for
each header, uses the component's public/transitive and private include/definition
context, and applies the existing strict C11 helper to every probe. It hashes the
repository-relative header path with SHA-256 for probe/target identity. No header
is exempted, no dialect check is relaxed, and no combined-header requirement is
introduced. Intentional combined/include-order checks remain separate work.

`tools/verify_c11_regression.py` loads the exact reviewed helper and guard through
`git show`. It requires both new F1 tests to fail assertions with no setup errors;
the separate real source TUs must first compile successfully. CI then executes
the full corrected architecture suite. Additional helper coverage checks private
include directories/definitions, colliding sanitized path spellings and duplicate
basenames, and non-C11 rejection in header-only Ports. Existing C17/GNU override
and strict extension tests remain intact.

The pinned actual ESP-IDF fixture now supplies separate private-header source TUs
and header-only Ports with matching guards. It compares all discovered headers
against the inventory, checks unique identities and one include per probe, and
requires exactly one compile command for every expected source and probe with
effective `-std=c11`. It does not substitute a weaker minimum command count for
the obsolete five-command assertion. CI retains generated sources, context,
inventories, compile commands, positive/negative logs and member checksums for
90 days. Regression-first failures are expected evidence, not hidden green tests.

## Current conflicts and preservation requirements

At intake, main is the #38 squash merge
`cc2066db6cd01bb2bfa72e304d530cab2963cc22`; the common base is
`4a21171353b72ceff62774ba114705e0c8be2c31`. Read-only `git merge-file -p`
analysis on copies of the reviewed branch/base/main found:

| Shared path | Conflict blocks |
|---|---:|
| `.github/workflows/firmware-build.yml` | 2 |
| `tools/class-s-gate.sh` | 1 |
| `tools/class-s-gate-selftest.sh` | 1 |

No merge, rebase, conflict resolution or #43 import belongs to this remediation.
The future separately authorized integration must preserve the merged #38 Python
scanner, BOM handling, trigraph-before-splice/comment handling, conservative
scanning, dependency/cross-layer policies, symlink/error/non-vacuity behavior and
all 59 current regressions. Do not select this old-base legacy grep gate wholesale.
Preserve #39's validated canonical detector, manifest and C11 checks while
migrating Python consumers and resolving shared workflow/wrapper/selftest edits.
Run all original/new tests and the invariant, host/target C11, actual IDF, Class B
and reference checks on the eventual combined SHA. A separate-main baseline or
either branch's CI cannot establish combined compatibility.

N6 remains **PARTIAL**. PR #43 at
`e65e6596b32d9fdc6d779cddfd6fa583f3031efc` requires a separate refresh and
independent review; its previous checks do not validate current main plus #39.
Older #39 statements that #38 was unmerged are historical checkpoints.

## Governance and claim limits

ADR-0001 remains **Proposed**; Firmware/RTOS consultation and the Owner's
Accept/Reject/Amend decision remain outstanding. Preserve the
[Owner's R1/R2 conditions](https://github.com/YerriSuyudHasim/noctinirs-2/pull/38#issuecomment-6233707924):
retain scanner/parser/manual/deferred limits, reassess unsupported production
mechanisms before use, preserve canonical-manifest compatibility, and make no
production conformance claim for NOT APPLICABLE. Manual review still covers
macro/transitive/generated/root/toolchain/build/link mechanisms and the Core
standard-header whitelist; `<stdatomic.h>` requires RTOS review. Unsupported
component CMake remains fail-closed.

The approved N5 staging exception remains active under
[hub approval](https://github.com/YerriSuyudHasim/ai-engineering-hub/issues/2#issuecomment-6236882761).
Restore real `class-u-host-tests` bound to GitHub Actions integration 15368 before
the first production `nn2_*` component/runtime merge (including Issue #13), or by
**2026-10-25T01:00:00Z**, whichever is earlier. At expiry restore unless a new
explicit Owner-approved extension is recorded. Infrastructure fixtures, synthetic
success and skips do not satisfy product Class U. This task changes no settings
and creates no restoration automation; restoration remains an open obligation.

All new tests are tooling/host or target-compiler build evidence. Production
Class S remains SKIPPED / **NOT APPLICABLE**, not conformance; product Class U,
hardware/HIL, timing, electrical and clinical claims are not established. Final
exact-SHA CI, artifacts, checksums, limitations and the independent re-review
request belong in the append-only PR handoff. Re-review readiness for F1 does not
complete N6 or authorize merging/closing Issue #11. Rollback is a reviewed revert
of the F1 remediation commit.
