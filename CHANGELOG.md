# Changelog

Notable changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
`0.1.0` is the development baseline, not a release declaration. No release date,
tag, milestone acceptance, or hardware validation is established by this file.

## Unreleased

The entries below record merged history reachable from `main` at
`5958a0eafec6b03c2b158c4026f3f10b8a3366d1` (GitHub API inspection:
2026-10-22T06:35:26Z). Dates below use UTC and describe repository events, not
releases. Open work is listed separately in [Pending work](#pending-work).

### Added

- **M0 baseline, 2026-10-16:** minimal ESP32-S3 / ESP-IDF v6.1 firmware scaffold,
  historical snapshots with an integrity manifest, and reference-integrity
  verification. Initial commit:
  [`cef4947abf644f6aad7eac0eb169978ae90d7c8f`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/cef4947abf644f6aad7eac0eb169978ae90d7c8f).
- **M0.1, 2026-10-16:** product/hub registration and governance fields under
  OWNER-M0-A1-001, including human owner, research-prototype risk class and
  evidence retention. [PR #5](https://github.com/YerriSuyudHasim/noctinirs-2/pull/5),
  merge [`52d9f85d6ce454b7e1ef97cc7908bbae92b49d4f`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/52d9f85d6ce454b7e1ef97cc7908bbae92b49d4f).
  Backup owner remains unassigned; see the
  [recorded continuity risk](https://github.com/YerriSuyudHasim/noctinirs-2/issues/1#issuecomment-6208381185).
- **M0.2, 2026-10-16:** pinned ESP-IDF v6.1 Class B build CI, PR-head checkout,
  toolchain/configuration/size/hash evidence and 90-day artifact retention.
  [PR #6](https://github.com/YerriSuyudHasim/noctinirs-2/pull/6), merge
  [`9f925bf85eabf2d718d03b6153d5132451eeda84`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/9f925bf85eabf2d718d03b6153d5132451eeda84).
  [Merge evidence](https://github.com/YerriSuyudHasim/noctinirs-2/issues/2#issuecomment-6207884155)
  records Class B PASS and the absent-layer Class S gate NOT APPLICABLE.
  Same-SHA binary reproducibility failed in M0; build success did not establish it.
- **M0.3, 2026-10-16:** ADR-0001 clean-architecture boundaries, normative layer
  paths and evidence semantics. [PR #7](https://github.com/YerriSuyudHasim/noctinirs-2/pull/7),
  merge [`062dc36008c45b04fe89e93521e0665d35c7c314`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/062dc36008c45b04fe89e93521e0665d35c7c314).
  The [merge record](https://github.com/YerriSuyudHasim/noctinirs-2/issues/3#issuecomment-6207884147)
  explicitly preserves **Proposed** status; merging documentation did not accept
  the ADR or prove implementation conformance.
- **Post-M0 merged documentation, 2026-10-16:** hardware/datasheet references
  ([PR #16](https://github.com/YerriSuyudHasim/noctinirs-2/pull/16) and subsequent
  main commits), ADR-0002 optical timing
  ([PR #22](https://github.com/YerriSuyudHasim/noctinirs-2/pull/22)), and ADR-0003
  task topology ([PR #27](https://github.com/YerriSuyudHasim/noctinirs-2/pull/27)).
  These documents do not establish measured hardware or real-time behavior.

### Changed

- ADR-0002 status recorded as Accepted via
  [PR #24](https://github.com/YerriSuyudHasim/noctinirs-2/pull/24) on 2026-10-16.
  ADR-0003 status recorded as Accepted via
  [PR #29](https://github.com/YerriSuyudHasim/noctinirs-2/pull/29) on 2026-10-16,
  followed by the dual-ADS task documentation in
  [PR #32](https://github.com/YerriSuyudHasim/noctinirs-2/pull/32), merge
  [`4a21171353b72ceff62774ba114705e0c8be2c31`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/4a21171353b72ceff62774ba114705e0c8be2c31).
  These are recorded document states, not new acceptance decisions by this change.
- **ADR-0001 bounded acceptance, 2026-10-20:** status on main is **Accepted**
  (bounded static architecture framework; not product implementation approval),
  recorded by [PR #44](https://github.com/YerriSuyudHasim/noctinirs-2/pull/44),
  merge [`8b61a87aa8c87c85067606ce9de80b7251551e61`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/8b61a87aa8c87c85067606ce9de80b7251551e61)
  at 2026-10-20T07:45:24Z. The earlier #7 merge record that preserved **Proposed**
  remains historical. This changelog does not re-decide the ADR.
  **Open item:** `docs/adr/0003-rtos-task-topology.md` still says ADR-0001
  remains Proposed. That sentence was not edited here.

### Fixed

- **M1.2 initial gate fix, 2026-10-16:** scanner exit-code handling,
  empty/missing-layer failure and Bash fixture invocation in
  [PR #18](https://github.com/YerriSuyudHasim/noctinirs-2/pull/18), merge
  [`96e68e1e052a5fce1ed006805132269cf34f1c1e`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/96e68e1e052a5fce1ed006805132269cf34f1c1e).
  That #18 merge did not itself accept the later hardening. #38 later merged
  separately; see [Merged history after `4a211713`](#merged-history-after-4a211713).

## Merged history after `4a211713`

Checked on the GitHub API at 2026-10-22T06:35:26Z against main
`5958a0eafec6b03c2b158c4026f3f10b8a3366d1`. Each merged SHA below is the pull
request `merge_commit_sha` and the commit on `main`. These are repository
events. They are not a release, a tag, or a new acceptance decision by this
changelog. The M0 reproducibility failure remains a historical fact.

### Added

- **M1.1 contracts, 2026-10-18:** C11 verification and canonical layers.
  [PR #39](https://github.com/YerriSuyudHasim/noctinirs-2/pull/39), merge
  [`5e45b398a6abfa6ec98de7f5d4814f13dad1900a`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/5e45b398a6abfa6ec98de7f5d4814f13dad1900a)
  at 2026-10-18T09:25:39Z.
  [Issue #11](https://github.com/YerriSuyudHasim/noctinirs-2/issues/11) is
  closed completed (2026-10-20T08:11:26Z).
- **N6 canonical manifest, 2026-10-18:** integration across #38 and #39.
  [PR #43](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43), merge
  [`d92796d73806e2e97092329be3741b3ddac19c54`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/d92796d73806e2e97092329be3741b3ddac19c54)
  at 2026-10-18T23:30:42Z.
- **M1.3 clean-build reproducibility evidence, 2026-10-19:**
  [PR #40](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40), merge
  [`4ee6d5412e31848d8f7a5681aa91853f5847ce8d`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/4ee6d5412e31848d8f7a5681aa91853f5847ce8d)
  at 2026-10-19T00:43:08Z.
  [Issue #9](https://github.com/YerriSuyudHasim/noctinirs-2/issues/9) is closed
  completed (2026-10-20T08:11:15Z). The
  [closeout](https://github.com/YerriSuyudHasim/noctinirs-2/issues/9#issuecomment-6286504277)
  limits that closure to the scaffold/pinned-target deliverable.
- **M1.5 fake-only component slice, 2026-10-21:**
  [PR #15](https://github.com/YerriSuyudHasim/noctinirs-2/pull/15), merge
  [`360cea4ccc53117d3fb09d411b27b6d61155fb2b`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/360cea4ccc53117d3fb09d411b27b6d61155fb2b)
  at 2026-10-21T13:12:58Z.
  [Issue #13](https://github.com/YerriSuyudHasim/noctinirs-2/issues/13) is
  closed completed (2026-10-22T06:10:45Z). This is not hardware validation.
- **Emitter-inactive ADS131M04 diagnostics, 2026-10-22:**
  [PR #36](https://github.com/YerriSuyudHasim/noctinirs-2/pull/36), merge
  [`f2d7c8cfecdc6570df155fac4d90625c16a7bf4b`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/f2d7c8cfecdc6570df155fac4d90625c16a7bf4b)
  at 2026-10-22T01:13:52Z.
  [Issue #35](https://github.com/YerriSuyudHasim/noctinirs-2/issues/35) remains
  open.
- **v0.1.x roadmap, 2026-10-22:** [PR #47](https://github.com/YerriSuyudHasim/noctinirs-2/pull/47),
  merge [`5958a0eafec6b03c2b158c4026f3f10b8a3366d1`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/5958a0eafec6b03c2b158c4026f3f10b8a3366d1)
  at 2026-10-22T06:28:28Z. The file records the plan. It is not a release.

### Fixed

- **M1.2 Class S gate hardening, 2026-10-17:**
  [PR #38](https://github.com/YerriSuyudHasim/noctinirs-2/pull/38), merge
  [`cc2066db6cd01bb2bfa72e304d530cab2963cc22`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/cc2066db6cd01bb2bfa72e304d530cab2963cc22)
  at 2026-10-17T16:56:28Z.
  [Issue #10](https://github.com/YerriSuyudHasim/noctinirs-2/issues/10) is
  closed completed (2026-10-22T06:10:45Z).

### Checked range #38–#47

| Record | API state | Merge commit |
|---|---|---|
| [PR #38](https://github.com/YerriSuyudHasim/noctinirs-2/pull/38) | MERGED 2026-10-17T16:56:28Z | `cc2066db6cd01bb2bfa72e304d530cab2963cc22` |
| [PR #39](https://github.com/YerriSuyudHasim/noctinirs-2/pull/39) | MERGED 2026-10-18T09:25:39Z | `5e45b398a6abfa6ec98de7f5d4814f13dad1900a` |
| [PR #40](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40) | MERGED 2026-10-19T00:43:08Z | `4ee6d5412e31848d8f7a5681aa91853f5847ce8d` |
| [PR #41](https://github.com/YerriSuyudHasim/noctinirs-2/pull/41) | OPEN, Draft, `merged=false` | none |
| [PR #42](https://github.com/YerriSuyudHasim/noctinirs-2/pull/42) | OPEN, Draft, `merged=false` (this change) | none |
| [PR #43](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43) | MERGED 2026-10-18T23:30:42Z | `d92796d73806e2e97092329be3741b3ddac19c54` |
| [PR #44](https://github.com/YerriSuyudHasim/noctinirs-2/pull/44) | MERGED 2026-10-20T07:45:24Z | `8b61a87aa8c87c85067606ce9de80b7251551e61` |
| [#45](https://github.com/YerriSuyudHasim/noctinirs-2/issues/45) | OPEN issue, not a pull request (pull API 404) | none |
| [PR #46](https://github.com/YerriSuyudHasim/noctinirs-2/pull/46) | OPEN, Draft, `merged=false` | none |
| [PR #47](https://github.com/YerriSuyudHasim/noctinirs-2/pull/47) | MERGED 2026-10-22T06:28:28Z | `5958a0eafec6b03c2b158c4026f3f10b8a3366d1` |

[PR #37](https://github.com/YerriSuyudHasim/noctinirs-2/pull/37) is closed and
unmerged (`merged=false`, `merge_commit_sha=null`, closed 2026-10-22T06:05:19Z).
It is not merged history.

## Pending work

Snapshot: 2026-10-22T06:35:26Z. These items are **not merged or released** at
the inspected main SHA. Milestone
[M1](https://github.com/YerriSuyudHasim/noctinirs-2/milestone/1) has no open
issues. Milestone
[v0.1.0 Communication Foundation](https://github.com/YerriSuyudHasim/noctinirs-2/milestone/2)
is milestone number 2. Their CI and author handoffs are not independent approval.

| Work | Record | State / remaining decision |
|---|---|---|
| M1.4 memory diagnosis | [Issue #12](https://github.com/YerriSuyudHasim/noctinirs-2/issues/12) (open, milestone v0.1.0 Communication Foundation), [Draft #41](https://github.com/YerriSuyudHasim/noctinirs-2/pull/41) at `b7ebdfa75b67cbd9aaa1bc7127c040537ef29e34` | Unmerged. Budgets remain proposed. Related [PR #37](https://github.com/YerriSuyudHasim/noctinirs-2/pull/37) is closed unmerged |
| M1.6 integration hygiene | [Issue #14](https://github.com/YerriSuyudHasim/noctinirs-2/issues/14) (open, same milestone), [Draft #42](https://github.com/YerriSuyudHasim/noctinirs-2/pull/42) | This changelog, PR template and traceability convention stay a review candidate until independently reviewed and Owner-authorized merge |
| Board-v1 packages | [Issue #45](https://github.com/YerriSuyudHasim/noctinirs-2/issues/45) open; [Draft #46](https://github.com/YerriSuyudHasim/noctinirs-2/pull/46) open | No merge SHA |
| ADR-0004 / timestamps | [Draft #55](https://github.com/YerriSuyudHasim/noctinirs-2/pull/55) open, Proposed, not on main; [Issue #56](https://github.com/YerriSuyudHasim/noctinirs-2/issues/56) open (milestone field null) | Per #56, the contract and the simulator conformance test are v0.1.0 work; real DRDY-latched implementation is v0.1.2/v0.1.3. Acceptance of the conformance work is **BLOCKED** until Agent 06 posts acceptance criteria on #56 and [#50](https://github.com/YerriSuyudHasim/noctinirs-2/issues/50). At this inspection #50 has no comments and #56's comments are Agent 05 mirrors |
| H-COMMS-1 | [Issue #53](https://github.com/YerriSuyudHasim/noctinirs-2/issues/53) open; [Draft #54](https://github.com/YerriSuyudHasim/noctinirs-2/pull/54) open | Parked for this convention: not a v0.1.0 merge gate. **Open item:** #53 is still in milestone v0.1.0, and `docs/ROADMAP.md` at `5958a0ea` still names H-COMMS-1 as the v0.1.0 Class H gate. This changelog does not amend that roadmap |

The [M0 closeout](https://github.com/YerriSuyudHasim/noctinirs-2/issues/8) and
[Owner addendum](https://github.com/YerriSuyudHasim/noctinirs-2/issues/8#issuecomment-6208383677)
retain the original limitations and decisions. Issues #1–#4 and #8 were later
closed by recorded triage; that does not establish every original acceptance
criterion. In particular, M0.4 was not completed in the closeout and was later
[superseded by the #26 survey](https://github.com/YerriSuyudHasim/noctinirs-2/issues/4#issuecomment-6227855977).
Issues #9, #10, #11 and #13 are closed completed. Memory diagnosis continues in
#12. Branch protection continues in #17. No historical implementation is
adopted by this changelog.

For new records, use the [traceability convention](docs/TRACEABILITY.md) and
[PR template](.github/pull_request_template.md). Move a pending item into merged
history only after checking its actual merge SHA; release entries require a
separate authorized release record.
