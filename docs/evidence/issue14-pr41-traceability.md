# Issue #14 retrospective traceability example: Draft PR #41

**Historical snapshot, 2026-10-17 UTC. This is an Integration Engineer's
traceability record, not an independent technical review, budget acceptance or
merge approval.** The #41 source branch is unchanged. Apply the
[convention](../TRACEABILITY.md); preserve this snapshot if later heads differ.

## Chain and required evidence

| Stage | Observed record | State / gap |
|---|---|---|
| Issue | [#12 M1.4 memory diagnosis](https://github.com/YerriSuyudHasim/noctinirs-2/issues/12), [M1](https://github.com/YerriSuyudHasim/noctinirs-2/milestone/1) | Independent budget review remains pending per author handoff |
| Branch | `diagnostics/12-memory-budget-regression`; main base `4a21171353b72ceff62774ba114705e0c8be2c31` | Base and head recorded separately |
| PR | [#41](https://github.com/YerriSuyudHasim/noctinirs-2/pull/41) | OPEN, DRAFT, `merged=false`, `merged_at=null` at inspection |
| Commit SHA | [`b7ebdfa75b67cbd9aaa1bc7127c040537ef29e34`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/b7ebdfa75b67cbd9aaa1bc7127c040537ef29e34) | Evidence below belongs to this full head only |
| CI run | Exact-head runs and jobs below; GitHub run/job API metadata inspected | Run success is not independent approval |
| Independent Review | [Author's role handoff](https://github.com/YerriSuyudHasim/noctinirs-2/pull/41#issuecomment-6232265303) to **Group 2 Hardware Specialist** | PENDING: submitted GitHub reviews = 0; requested reviewers = []; role handoff exists, named reviewer assignment and verdict not established |
| Owner Decision | Author handoff requests budget adoption and subsequent merge decisions | PENDING: no Owner decision/merge authorization established by inspected PR body, comments or reviews |
| Merge SHA | UNMERGED | No actual merge SHA; ignore the open PR's synthetic `merge_commit_sha` |

Scope: reporting workflow, evidence collection/comparison tooling, proposed
thresholds, fixtures/tests and diagnosis. Nine changed files were verified from
the PR file inventory:

```text
.github/workflows/memory-report.yml
docs/M1_ISSUE12_MEMORY_DIAGNOSIS.md
docs/evidence/issue12-baseline.json
tests/memory/fixtures/README.md
tests/memory/fixtures/size.json
tests/memory/test_memory.py
tools/collect_memory.py
tools/memory-thresholds.proposed.json
tools/memory_report.py
```

No `reference/**` or production firmware/configuration file is in that diff.
This is an inventory observation, not a technical approval of the tooling.

## CI provenance

All runs below report full `head_sha`
`b7ebdfa75b67cbd9aaa1bc7127c040537ef29e34`, attempt **1**, conclusion **success**.
Run metadata and individual job conclusions were fetched through GitHub via
Composio on 2026-10-17 UTC. The existing reference workflow's PR checkout is a
synthetic merge tree; the exact-head push run is separate corroboration.

| Workflow | pull_request run | push run |
|---|---|---|
| Memory diagnostics | [39020593973](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593973) | [39020590330](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020590330) |
| Firmware build | [39020593966](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593966) | [39020590311](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020590311) |
| Reference integrity | [39020593954](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593954) | [39020590337](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020590337) |

| PR job | Raw conclusion | Supported scope |
|---|---|---|
| [memory-report (informational)](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593973/job/117361444563) | success | Build/report comparison; author handoff records zero deltas, not runtime validation |
| [memory-tool-tests](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593973/job/117361444774) | success | Author handoff records 31/31 host-tool methods, no skips |
| [class-s-gate-selftest](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593966/job/117361444621) | success | Gate tooling fixtures |
| [Layer detect](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593966/job/117361444638) | success | Informational detection only |
| [class-b-esp-idf-build](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593966/job/117361444693) | success | Compile/link evidence |
| [class-s-gate-invariant](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593966/job/117361470862) | success | Invariant only |
| [class-s-dependency-gate](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593966/job/117361471403) | **skipped** | **NOT APPLICABLE, not PASS**: normative production layers absent |
| [reference-integrity](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593954/job/117361444497) | success | PR merge-tree integrity; exact-head push corroboration above |

## Evidence classification and limits

The statuses below transcribe the linked author evidence and API metadata for
traceability. They do not claim that the Integration Engineer reran #41 tests,
recomputed its artifact checksums or independently reviewed its diagnosis.

| Class / claim | Status | Evidence / limitation |
|---|---|---|
| S — reporting/configuration inventory and gate fixtures | PASS | File inventory, gate selftest and source-linked diagnosis; author-side static evidence only |
| S — production layer conformance | NOT APPLICABLE | Gate skipped; no normative layers; no conformance evidence |
| B — firmware compile/link and memory-report builds | PASS | Exact-head build job; diagnosis/handoff supplies environment and commands |
| U — memory-report host tooling | PASS | Author records 31 executed methods; does not establish Core/Application unit coverage |
| I — product integration / combined PRs | NOT RUN | No combined tree or device integration established |
| H — hardware/on-target | NOT RUN | No device operation authorized or evidenced |
| R — memory-report tooling regression | PASS | Same scoped host suite; does not establish firmware runtime regression coverage |

The [versioned diagnosis](https://github.com/YerriSuyudHasim/noctinirs-2/blob/b7ebdfa75b67cbd9aaa1bc7127c040537ef29e34/docs/M1_ISSUE12_MEMORY_DIAGNOSIS.md)
and [author handoff](https://github.com/YerriSuyudHasim/noctinirs-2/pull/41#issuecomment-6232265303)
record ESP-IDF v6.1 `fff9895c82d744c7237be8847347bdd1b07c6643`, ESP32-S3,
GCC 15.2.0 / esp-15.2.0_20251204, Python 3.12.3, CMake 4.0.3, Ninja 1.11.1,
the pinned SDK image, collection commands/configuration and build UTCs.
Board/fixture is not applicable to host builds. Their
[memory artifact](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020593973/artifacts/12096109980)
has author-recorded ZIP SHA-256
`4d1a64a10bdb5d671d00acd23c6e03ee91a6344e7737936393165f31d76ad299`
and 90-day retention. This record links the existing checksum; it is not a fresh
artifact verification or an extension of retention.

## Dependencies, decisions and next action

PR #40 at `f97767035e0ab4f801e655ec751141314668c407` is an **unmerged
reference-only dependency**, not incorporated code or an accepted reproducibility
fix. Related unmerged heads are #38 at
`71bd7ae4413ce06cf12d24b384ebac554e3a647a` and #39 at
`789b4fa8bb2904a85116c6c6116973c348565c05`.
The author handoff flags related Draft #37 for
scope reconciliation; separate #38/#39/#40/#41 CI does not prove combined
compatibility. Their independent chains and final integration evidence remain
necessary. No source-branch modification is made by this example.

Issue #12's independent budget-review criterion remains **PARTIAL**. Thresholds
are proposed, not Owner-accepted production limits. Peak runtime heap, stack
high-water marks, fragmentation, DMA availability, ISR timing, electrical/optical
safety and clinical validity are not established by static build reports.

Next action: a non-author **Group 2 Hardware Specialist** verifies this SHA,
raw evidence and budget assumptions and records findings/decision. The Owner
then decides budget adoption and any later merge after required checks/reviews.
Rollback remains a reviewed revert of reporting tooling/docs/workflow, not a
firmware or hardware recovery claim. Any new head requires fresh evidence and
review; this historical example must not be silently updated into approval.
