# N6 canonical layer manifest integration

Accountable: Group 1 Architecture Integration Engineer. Scope: Issues [#11](https://github.com/YerriSuyudHasim/noctinirs-2/issues/11) and [#10](https://github.com/YerriSuyudHasim/noctinirs-2/issues/10), static architecture integration only. Independent Group 2 review and Owner architecture decisions remain pending. ADR-0001 remains **Proposed**. No hardware evidence is claimed.

## Recorded inputs and source-of-truth decision

Discovery on 2026-10-17 re-fetched refs and checked GitHub metadata, complete issue bodies/comments, PR descriptions/commits/files, and all review handoffs. Neither PR had submitted reviews or inline review comments. Product and hub AGENTS.md, both project records, ADR Q-10/Q-11, N5/N6, existing workflows, gate wrappers, scanner and tests were read before implementation. Hub main was unchanged at `4d8772b`; existing task worktrees were left untouched. Initial reference integrity: 319 files PASS. Initial product checkout was clean at PR #38's head.

| Input | Full SHA |
|---|---|
| main / common base | `4a21171353b72ceff62774ba114705e0c8be2c31` |
| PR #38 | `71bd7ae4413ce06cf12d24b384ebac554e3a647a` |
| PR #39 | `789b4fa8bb2904a85116c6c6116973c348565c05` |
| Local merge preserving #38 | `19110835e62e93f6d9c9a2bf1250a1d9ec10a7bf` |
| Combined candidate | The full source SHA recorded in this Draft PR's append-only integration evidence / Group 2 handoff; also `git rev-parse HEAD` on the submitted branch. The containing commit cannot embed its own hash. |

Chosen strategy **C: separate integration Draft PR**, branch `integration/11-n6-canonical-manifest`. Amending #38 or #39 would alter an existing independent-review boundary. Both original heads/histories and historical evidence are preserved. No push to main, merge into main, history rewrite or issue closure is authorized by this report.

Authoritative file: [`tools/layers.json`](../tools/layers.json), schema v2. It owns identities, paths, dependency allow-lists, header ownership prefixes, forbidden literal include patterns and source/header suffixes. `requires` is permission, not a requirement to declare every permitted edge. Every unlisted component dependency is forbidden. Intra-layer includes are allowed; self component dependencies are invalid. The canonical matrix is core → none, ports → core, app → core and ports. Its enforcement is still proposed for Owner architecture acceptance; tooling implementation can be reviewed independently of that decision.

Consumers:

| Consumer | Use |
|---|---|
| `tools/layer_contract.py` | Strict schema, duplicate/conflict/cycle validation; real-directory detection; unknown `nn2_*` rejection; no permissive fallback |
| `tools/class-s-gate.py` | Full policy; derives forbidden cross-layer edges as complement of allowed dependencies; scans all recognized files inside each layer and literal CMake registration |
| `tools/class-s-gate.sh` | Thin entry point to that same Python gate; interpreter errors fail closed |
| Firmware workflow | Canonical detector, sole gate wrapper, self-tests, architecture tests and invariant |
| `cmake/nn2_c11.cmake` | Validated layer paths for unchanged root C11 enforcement/header probes |
| IDF fixture and regression tests | Validated canonical input; explicit independent test oracles are fixtures, not alternate production configuration |
| ADR-0001 §3.5 | Explicit normative manifest reference; diagrams/examples explain the policy |

Removed production duplication: Python `LAYERS` dependency/path map, `CROSS` map, `PLATFORM` regex and `SOURCE_EXT` set; inline workflow path enumeration; alternative Bash grep scanner. No second normative gate remains. The manifest retains all #38 forbidden patterns, adds the plural Ports header ownership alias, and deterministically derives cross-layer checks. Test expectation matrices intentionally remain explicit to detect accidental policy weakening.

## Reproducible rehearsal and conflicts

```text
git fetch origin
git worktree add ../noctinirs-2-n6 -b integration/11-n6-canonical-manifest 4a21171353b72ceff62774ba114705e0c8be2c31
git merge --no-ff 71bd7ae4413ce06cf12d24b384ebac554e3a647a -m "integration: preserve PR #38 history for N6 rehearsal (refs #11)"
git merge --no-ff --no-commit 789b4fa8bb2904a85116c6c6116973c348565c05
# Apply the reviewed reconciliation diff in the integration merge commit.
# Stage and commit; do not resolve by blindly selecting one PR's entire tree.
```

Conflicts were exactly `.github/workflows/firmware-build.yml`, `tools/class-s-gate.sh`, and `tools/class-s-gate-selftest.sh`.

- Workflow: kept #39's canonical detector, architecture/C11 jobs and #38's Python regression execution; retained invariant semantics and exact PR-head checkouts. Gate output is captured with pipefail and diagnostics; every nonzero production gate result fails CI, including exit 3 when detection says present. Added 90-day scanner test artifacts. Reference workflow now explicitly checks out/logs the same source SHA.
- Wrapper: retained #38's Python scanner; removed #39/main's independent grep scanner. Python can be selected for local tooling, with no policy fallback. Unexpected interpreter exits normalize to FAIL.
- Bash self-test: retained #38's literal CMake registrations and Windows permission-skip distinction; replaced #39's obsolete grep-error injection with interpreter-failure injection. Python traversal-error injection remains in the 51-method suite.

Additional reconciliation files: `tools/layers.json`, `tools/layer_contract.py`, `tools/class-s-gate.py`, `tests/gate-fixtures/test_issue10_gate.py`, `tests/architecture/test_manifest.py`, new `tests/architecture/test_n6.py`, ADR-0001, historical decision packet banner, this report, and reference workflow. The original PR #39 root CMake hook, helper/guard, port template and IDF/compiler fixtures are retained. No component implementation, production firmware, Kconfig, reference snapshot or governance policy is changed by reconciliation. Original #38 audit documents remain historical evidence.

## Validation and evidence boundaries

Exact-SHA commands (run again by CI on the submitted candidate):

```text
python3 -m unittest discover -s tests/gate-fixtures -v
bash tools/class-s-gate-selftest.sh
python3 -m unittest discover -s tests/architecture -v
NN2_TEST_CC=xtensa-esp32s3-elf-gcc python3 -m unittest discover -s tests/architecture -p test_c11.py -v
python3 tools/verify_c11_idf.py
idf.py set-target esp32s3
idf.py build
idf.py size
pwsh -File tools/verify-reference.ps1
git diff --check
```

Author-side workspace checks: 51 Issue #10 methods PASS; architecture suite 40/40 PASS (15 manifest, 6 C11, 19 N6); target compiler C11 suite 6/6 PASS; Bash 20 PASS with one explicit Windows POSIX permission SKIP; reference integrity 319 files PASS. These are synthetic tooling/build fixtures and repository integrity checks, not product-layer conformance. The initial unconfigured Windows runs failed on the `python3` Store alias and missing IDF environment; corrected process-local paths/shim reruns are separate results. No dependencies or machine settings were installed or changed.

CI source is the exact submitted head (`SOURCE_SHA`), not either prerequisite's previous green SHA and not an implicit PR merge ref. The workflow retains architecture logs, Class S self-test logs and Class B configuration/binaries/hashes for 90 days. The append-only PR integration evidence records the candidate SHA, run/job/artifact URLs, tool versions, outcomes and any deviations after jobs complete. This versioned procedure alone is not a claim that pending CI passed.

| Required adversarial case | Fixture evidence |
|---|---|
| Valid canonical manifest | N6 canonical dependency oracle + manifest canonical paths |
| Missing/malformed input | CLI/schema and gate-before-absence tests; no NOT APPLICABLE fallback |
| Unknown layer | Detector and gate unknown file/directory/link fixtures |
| Forbidden Core/Ports edges | N6 explicit CMake edges + #38 include matrix |
| Valid App edges | N6 both dependencies and allowed include fixture |
| Header/include violations | All 69 original layer/header matrix cases, relative/prefixed/lexical variants, plural Ports alias |
| Hidden/indirect attempts | Dynamic expressions, extra commands, nested CMake, external paths, manager manifests, symlinks fail; macro blind spot explicitly characterized |
| Duplicate/conflicting definitions | Duplicate keys/roles/dependencies, overlapping prefixes, unknown/self/cyclic edges fail |
| Missing source directory | Missing layer fails once any layer exists |
| No normative source files | Empty directories select the gate, then FAIL; all absent remains NOT APPLICABLE |
| Determinism | Repeated multi-violation output equality and sorted traversal |
| CI propagation | Actual workflow shell: eight invariant states and five gate exit results; six original actual detection methods |

Q-10: unchanged root hook, ISO C11 properties/flags, forced dialect guard, header probes and negative C17/GNU11/extension tests. Component CMake remains within #38's restricted literal-registration subset. Q-11: literal scanner guarantees preserved; macro/G3, transitive/G4, generated/G5 and `__has_include` responsibilities/limits remain explicit. No skipped result is PASS. With all production layers absent, detector is informational, production dependency job is SKIPPED / NOT APPLICABLE; invariant and fixture success do not establish product conformance.

## Residual risks and required review

The scanner is not a preprocessor, general CMake evaluator or link graph analyzer. Macros, conditional branches, transitive/header-shadow dependencies, generated headers, root/toolchain mutation, callbacks and link symbols retain manual/deferred risk under ADR §3.10. The N6 macro characterization deliberately demonstrates that limitation; forbidden architecture remains forbidden even when undetected. The full Core standard-header whitelist and `<stdatomic.h>` acceptance remain manual review. Recognized alternative suffixes are scan coverage, not authorization to introduce non-C11 production code. Valid policy edits can change architecture and require independent review; schema validation does not replace that review.

No product inner layers exist. Production host unit/dependency evidence must be added when they land. Class B proves compile/link only; Class H is NOT RUN. N5 branch protection remains separately Owner-blocked; no repository setting is changed. Other pending work, especially PR #37's Class B/memory evidence overlap, is not incorporated and must be preserved in its later integration.

## Merge ordering, rollback and Group 2 handoff

Required order after independent reviews and explicit Owner approval: **PR #38 → PR #39 → this integration PR**. This Draft is a rehearsal carrying both histories; it is **not independently mergeable** before those prerequisites. The #39 merge must preserve #38's hardened Python wrapper and self-test registrations while adopting its canonical detector/C11 jobs; use the documented conflict resolutions, never reinstate the grep gate. Maintainers must then apply the N6 reconciliation, compare the resulting tree to this candidate and rerun all checks on the actual final merge candidate SHA. Any prerequisite head change invalidates the pinned rehearsal and requires fresh integration/review. No agent performs these merges.

Rollback before merge: abandon this branch/Draft without altering either source PR. After authorized integration: reviewed revert of the N6 reconciliation as a unit, restoring a coherent policy/consumer version; rerun required CI. Do not revert the manifest alone or select a permissive fallback. No deployed firmware/hardware rollback is involved.

Group 2 reviewer must independently verify the submitted SHA and diff, one normative manifest/all consumers, Core/Ports rejection and App permission, malformed-input fail-closed behavior, original 51 regressions, Q-10/Q-11 preservation, exact-SHA combined CI/artifacts, prohibited-path absence, Proposed ADR status and prerequisite ordering. Record findings or approval in the PR; this author-side handoff is not approval. Firmware/RTOS consultation remains required for the architecture decision. Issue #11 remains open for independent review and Owner decisions even when N6 implementation is ready.
