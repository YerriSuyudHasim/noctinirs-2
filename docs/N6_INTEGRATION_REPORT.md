# N6 canonical policy integration candidate — current state

Integration author: Codex / NoctiNIRS 2 Integration Engineer, 2026-10-18 UTC.
This report supersedes the stale candidate's current-state claims. Historical
report remains at `e65e6596b32d9fdc6d779cddfd6fa583f3031efc` in Git.
This is author-side preparation for independent review, never self-approval.
ADR-0001 remains **Proposed**. Issues #10/#11 remain open in substance.

## Verified inputs and ancestry

| Input | SHA |
|---|---|
| Current main / #38 squash | `cc2066db6cd01bb2bfa72e304d530cab2963cc22` |
| Corrected F1-reviewed #39 | `128755a5c25ff95b30ae99c2205613aa028f72ac` |
| Refreshed prerequisite #39 | `036927a0b9d7159e3516c1bb4970e962383243cd` |
| Stale #43 | `e65e6596b32d9fdc6d779cddfd6fa583f3031efc` |

All original remote heads matched before mutation. Isolated detached worktrees
preserve unrelated checkouts. #39 merged current main without rewriting history;
#43 merges that refreshed #39. Because #39 contains the actual #38 squash SHA,
both current main and corrected #39 are ancestors of this candidate. Old #38
source ancestry alone is not used as proof that current main is incorporated.
Final integration SHA and parent/tree identities are recorded in the PR handoff.

Required future merge order: **#39 before #43**; #38 is already merged. Neither
PR is merged by this task. No pending #40 content is imported. Both draft states
remain unchanged. Final source-HEAD CI is required; prior source CI is insufficient.

## Deliberate conflict resolutions

- Firmware workflow: retain canonical detector, actual gate invocation, invariant,
  all 59 gate expectations, corrected per-header C11 regression/target/IDF checks
  and generated-evidence retention. Combine individual regions, not a whole-file
  choice. Keep gate log artifacts on the final candidate.
- Scanner add/add: keep #43's complete manifest consumer and sorted diagnostics;
  transplant main's complete literal lexer, including BOM removal and trigraph
  translation before splicing/comments, plus conservative inactive-mode scanning.
  Restricted CMake parser, symlink/error/non-vacuity rules remain unchanged.
- Regression add/add: keep all current main expectations and #39's real
  detector harness setup. No failed expectation is deleted or weakened.
- Wrapper: retain the thin canonical Python entry point; malformed/missing policy
  fails before absence and unexpected interpreter results normalize to FAIL.
- Selftest: retain real component registrations, Windows permission limitation and
  deterministic interpreter failure. Python suite separately injects traversal errors.
- Per-header helper, dialect guard, C11 tests, reviewed-helper reproduction runner
  and pinned actual-IDF fixture are identical to refreshed #39.

## Canonical policy and every production consumer

[`tools/layers.json`](../tools/layers.json), schema v2, is the sole configurable
static policy: layer identities/paths, allowed dependencies, header ownership
prefixes, forbidden literal include expressions and recognized source extensions.
`requires` grants permission; it does not require every allowed dependency.

| Consumer | Validated use |
|---|---|
| layer_contract.py | Strict duplicate-key/schema/type/path/prefix/dependency/cycle checks; directory detection; no fallback |
| class-s-gate.py | Load full policy before NOT APPLICABLE; derive paths, allowed edges, forbidden cross-layer complements, prefixes, platform patterns and extensions |
| class-s-gate.sh | Invoke that sole scanner; normalize interpreter failure |
| firmware-build.yml | Canonical detector, sole gate, full regressions, actual invariant and architecture/C11 checks |
| nn2_c11.cmake / root CMake | Validated manifest paths and configure dependencies; independent strict-C11 probes unchanged |
| actual-IDF fixture | Load validated rows; retain exact source/header/probe/command coverage |
| ADR-0001 | Normative manifest link, with diagrams/examples explanatory |

No independent production LAYERS/CROSS/PLATFORM/SOURCE_EXT map or grep scanner
remains. Fixed schema role/path constraints are validation, not configurable
policy copies. Explicit test oracles and synthetic fixture registrations remain
independent to detect policy weakening. Alternate fixture manifests are supplied
explicitly and validated; there is no missing-policy or schema-v1 fallback.
Mutation fixtures prove allowed edges, forbidden patterns, ownership prefixes and
extensions affect scanner verdicts. All original 59 regressions remain applicable.

## Evidence, limits and governance

Author-side results, final exact-SHA CI links, artifact identities/digests and
resulting tree differences are in the append-only PR handoff. CI retains generated
probes, fixture headers, TSV inventories, compile commands, logs and checksum
inventories, including hidden Ninja metadata, for 90 days. Preserve controlled
copies before expiry when needed. Initial local setup/harness failures are retained;
only successful reruns are counted as success.

This is the **complete intended N6 implementation candidate**, subject to
independent integration review. #39 remains the partial-N6 prerequisite. Tooling
completion does not accept the architecture or prove production conformance.
Production Class S is SKIPPED / **NOT APPLICABLE**, not PASS. Product Class U,
hardware/HIL, timing, electrical, field and clinical validation are NOT RUN.
Class B proves build/link only; synthetic fixtures prove their tested tooling.

R1/R2 remain: scanner/parser/macro/transitive/generated/header-shadow/root/
toolchain/build/link/standard-header limits retain manual/deferred review. Dynamic
or unsupported component CMake remains fail-closed. stdatomic requires RTOS
review. Firmware/RTOS consultation, independent non-author review and Owner
Accept/Reject/Amend decision remain pending; ADR stays Proposed.

The approved [N5 restoration obligation](https://github.com/YerriSuyudHasim/ai-engineering-hub/issues/2#issuecomment-6236882761)
requires meaningful real class-u-host-tests bound to Actions integration 15368
before the first production nn2_* component/runtime merge (including #13), or
2026-10-25T01:00:00Z, whichever is earlier. Restore at expiry absent a new explicit
Owner-approved extension; empty/synthetic/skipped success cannot substitute.

No settings, reference snapshots, other PRs, issues, draft states, releases or
hardware are changed. Rollback requires a reviewed revert of the reconciliation
as a coherent policy/consumer unit and a fresh verification run. A future change
to prerequisite heads requires new integration, exact-SHA CI and review.
