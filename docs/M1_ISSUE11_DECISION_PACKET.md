# Issue #11: Owner architecture decision packet

Prepared by Group 1 Architect Lead / Orchestrator, 2026-10-17. ADR-0001 remains **Proposed**. This packet is an author recommendation, not independent approval or merge authorization.

## Proposed decision

**Recommendation: Amend.** Review the proposed Q-10/Q-11 decisions and port template now; complete the cross-PR N6 integration and independent review before accepting ADR-0001. OWNER DECISION REQUIRED: Accept, Reject, or Amend the final architecture revision after specialist/independent review. No status change, merge, release, tag or issue closure is performed here.

## Discovery and scope

Base: current `main` @ `4a21171353b72ceff62774ba114705e0c8be2c31`. Dedicated branch: `architecture/11-m1-1-contract-finalization`. Read product AGENTS.md (no nested active instructions), Issue #11 body and its one proposal comment, Issue #10, PR #38 body and both handoffs, ADR-0001, all CI, component/port/governance documentation and hub AGENTS.md/project registration. Hub policy blob verified via connected GitHub: `09f00c28699eff2ac572afe71de76e03aeedca34`. The hub has no configured automatic Reviewer workflow; its portable handoff is an issue/PR record.

Open PR overlap: #38 changes gate/parser/detection; #37 changes Class B evidence; #15 contains unmerged component skeletons. None is assumed merged or imported. Other open PRs observed: #20 hardware documentation, #28 toolchain notes, #34 Wi-Fi, #36 ADS131. This change touches no hardware, Kconfig, reference snapshot, network behavior or component implementation.

## Architecture changes and rationale

| Topic | Proposal / evidence point | Alternatives considered |
|---|---|---|
| Q-10 | Root hook after `project()` applies target properties plus explicit C11/pedantic flags and forced dialect guard. Compile every layer's headers in object probes; reuse helper on host. | C_STANDARD alone can decay/allow extensions or lose to raw options; global C11 would affect SDK/adapters; per-component commands conflict with PR #38's parser. |
| Q-11 | ADR 3.10 separates literal scanning, compiler/build evidence, manual review and deferred coverage. | Full preprocessor/build-graph analysis exceeds this issue; grep-only cannot establish macro/transitive absence. |
| N6 | JSON manifest and strict standard-library validator; CI detector, current main gate and root C11 hook consume it. Schema and ownership are in ADR 3.5. | YAML adds dependency; duplicated workflow arrays drift; generated documentation would add tooling without improving this three-row source. |
| Port contract | `docs/templates/port-interface-contract.md` specifies ownership, allocation, errors/errno, lifecycle, cleanup, contexts, timing, concurrency and verification. | Header-only prose without a checklist is easy to omit; a concrete example driver would expand firmware scope. |

Pinned IDF 6.1 selects **gnu23**, not the older comment's assumed gnu17. The guard tests exact `__STDC_VERSION__` and strict ANSI mode, catching later C17/GNU11 overrides. Compiler probes verify the enforcement mechanism; absent product layers do not establish production C11 or architecture conformance.

## Acceptance traceability (Issue #11 body)

| ID | Acceptance criterion | Status / evidence |
|---|---|---|
| AC-1 | Q-10 ADR rationale and verifiable enforcement | PASS implementation: ADR 3.9, `cmake/nn2_c11.cmake`, compiler fixtures and actual IDF fixture; final executed results are posted on the PR at its exact SHA. |
| AC-2 | Q-11 strategy and residual risk | PASS documentation: ADR 3.10; product host/dependency evidence explicitly deferred until layers exist. |
| AC-3 | One machine-readable list; ADR/workflow consume it; no independent list | PARTIAL across PRs: independent main-based portion implemented and tested; PR #38's Python map migration remains integration dependency. |
| AC-4 | Committed/referenced port template | PASS: ADR 3.4/10 links the template. |
| AC-5 | Independent non-author review recorded | BLOCKED pending independent Reviewer; author cannot satisfy this criterion. |
| AC-6 | Owner decision summary posted; status only by Owner | PASS packet supplied in draft PR; final Owner decision remains required; ADR Proposed unchanged. |

## Issue #10 integration contract

PR #38 remains draft/open at `71bd7ae4413ce06cf12d24b384ebac554e3a647a`; it has not been modified. Confirmed from the exact dependency diff: Python `LAYERS` combines names and dependency allow-lists; its parser accepts one literal registration and rejects added component commands. This issue's root C11 hook leaves component files in that subset. The existing main Bash scanner's pattern/exit behavior is unchanged; only its path list is loaded from JSON. Its selftest explicitly skips the ineffective Windows POSIX-permission fixture and adds a deterministic grep-error injection on every platform. No second include/dependency scanner is added.

Integration Agent must, after an authorized integration base is available:

1. Resolve shared `.github/workflows/firmware-build.yml`, `tools/class-s-gate.sh` and selftest conflicts; preserve canonical detection and PR #38's hardened wrapper/gate.
2. Load validated paths in `tools/class-s-gate.py` from the manifest. Keep `REQUIRES` allow-lists and CROSS rules keyed by semantic role; do not silently broaden them. Missing/malformed config must fail before NOT APPLICABLE.
3. Update PR #38's tests that extract the former inline detector to execute the manifest consumer and exercise malformed/symlink/unknown paths. Run all 51 methods and Bash fixtures; preserve explicit no-layer and empty-layer behavior.
4. Re-run main build, actual C11 fixture, Class S selftests/invariant, reference integrity and independent review on the new combined SHA. Neither branch's prior green CI proves combined compatibility.

This is an explicit integration dependency, not authorization to merge/cherry-pick PR #38. N6 is not marked complete. PR #37 also overlaps the Class B workflow; preserve its additional memory evidence when integrated later.

## Verification and evidence responsibilities

Local commands: `python -m unittest discover -s tests/architecture -v`; `NN2_TEST_CC=xtensa-esp32s3-elf-gcc python -m unittest discover -s tests/architecture -p test_c11.py -v`; `python tools/verify_c11_idf.py` in the pinned IDF environment; `bash tools/class-s-gate-selftest.sh`; `pwsh -File tools/verify-reference.ps1`; `git diff --check`. CI runs these contract checks plus normal Class B and reference integrity; exact counts/conclusions/run URLs are recorded in the append-only PR handoff after the final commit. Tests write only temporary build fixtures. Host/target contract fixture success is not product Class U or Class H evidence.

Verifier owns exact-head evidence and artifact retention; Firmware owns include roots/build provenance; Architect/Reviewer inspect macro conditionals, implicit dependencies and link/callback targets. C11 guards do not detect deliberate compiler/macro spoofing, C++/assembly or arbitrary root build mutation. Full transitive dependency collection/allow-list automation and real product host tests are deferred; no such coverage is claimed. Generated inner-layer headers are unsupported without a reviewed exception. `<stdatomic.h>` remains manual RTOS review. Timing, electrical behavior and hardware safety are NOT RUN.

## Outstanding decisions, risks and recovery

- OWNER DECISION REQUIRED: final ADR Accept/Reject/Amend; Firmware/RTOS agreement and independent Reviewer findings must be considered first.
- External dependency: PR #38 integration described above; no full N6 completion claim.
- Independent Reviewer must verify this SHA and record approval or blocking findings. Handoff alone is not approval.
- N5 branch protection remains the Owner's separate Issue #10 decision; no settings change here.
- Required product host dependency/branch evidence cannot exist while production layers are absent. Future layer PRs must add it before claiming the corresponding conformance.
- Rollback: reviewed revert of this issue's commits. No flash or deployed behavior exists to roll back. The shared manifest adds Python 3 as a build configuration requirement (already present in ESP-IDF and CI).

Final readiness is reported on the PR after CI. If AC-3 or AC-5 remains incomplete, verdict is **NOT REVIEW READY** under the assignment's definition, even when all executable checks pass.
