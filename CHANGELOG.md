# Changelog

Notable changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
`0.1.0` is the development baseline, not a release declaration. No release date,
tag, milestone acceptance, or hardware validation is established by this file.

## Unreleased

The entries below record merged history reachable from `main` at
`4a21171353b72ceff62774ba114705e0c8be2c31` (inspection: 2026-10-17 UTC).
Dates below use UTC and describe repository events, not releases. Open work is listed
separately in [Pending M1 work](#pending-m1-work).

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

### Fixed

- **M1.2 initial gate fix, 2026-10-16:** scanner exit-code handling,
  empty/missing-layer failure and Bash fixture invocation in
  [PR #18](https://github.com/YerriSuyudHasim/noctinirs-2/pull/18), merge
  [`96e68e1e052a5fce1ed006805132269cf34f1c1e`](https://github.com/YerriSuyudHasim/noctinirs-2/commit/96e68e1e052a5fce1ed006805132269cf34f1c1e).
  This earlier merge does not imply acceptance of the later hardening Draft #38.

## Pending M1 work

Snapshot: 2026-10-17 UTC. These items are **not merged or released** at the
inspected main SHA. Their CI and author handoffs are not independent approval.

| Work | Record | State / remaining decision |
|---|---|---|
| M1.1 architecture finalization | [Issue #11](https://github.com/YerriSuyudHasim/noctinirs-2/issues/11), [Draft #39](https://github.com/YerriSuyudHasim/noctinirs-2/pull/39) | Independent review and Owner decision pending; ADR-0001 remains Proposed; N6 cross-PR integration incomplete |
| M1.2 further gate hardening | [Issue #10](https://github.com/YerriSuyudHasim/noctinirs-2/issues/10), [Draft #38](https://github.com/YerriSuyudHasim/noctinirs-2/pull/38) | Independent review pending; combined #38/#39 integration unverified |
| M1.3 reproducibility | [Issue #9](https://github.com/YerriSuyudHasim/noctinirs-2/issues/9), [Draft #40](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40) | Candidate two-build evidence; independent configuration review and Owner merge decision pending; M0 failure remains historical fact |
| M1.4 memory diagnosis | [Issue #12](https://github.com/YerriSuyudHasim/noctinirs-2/issues/12), [Draft #41](https://github.com/YerriSuyudHasim/noctinirs-2/pull/41) | Independent Hardware Specialist review and Owner budget/merge decisions pending; budgets remain proposed; related [Draft #37](https://github.com/YerriSuyudHasim/noctinirs-2/pull/37) needs scope reconciliation |
| M1.5 first component slice | [Issue #13](https://github.com/YerriSuyudHasim/noctinirs-2/issues/13), [Draft #15](https://github.com/YerriSuyudHasim/noctinirs-2/pull/15) | Unmerged candidate; no completion or combined integration claim made here |
| M1.6 integration hygiene | [Issue #14](https://github.com/YerriSuyudHasim/noctinirs-2/issues/14) | This changelog, PR template and traceability convention are a review candidate until independently reviewed and Owner-authorized merge |

The [M0 closeout](https://github.com/YerriSuyudHasim/noctinirs-2/issues/8) and
[Owner addendum](https://github.com/YerriSuyudHasim/noctinirs-2/issues/8#issuecomment-6208383677)
retain the original limitations and decisions. Issues #1–#4 and #8 were later
closed by recorded triage; that does not establish every original acceptance
criterion. In particular, M0.4 was not completed in the closeout and was later
[superseded by the #26 survey](https://github.com/YerriSuyudHasim/noctinirs-2/issues/4#issuecomment-6227855977).
Reproducibility continues in #9, architecture in #11, memory in #12 and branch
protection in #17. No historical implementation is adopted by this changelog.

For new records, use the [traceability convention](docs/TRACEABILITY.md) and
[PR template](.github/pull_request_template.md). Move a pending item into merged
history only after checking its actual merge SHA; release entries require a
separate authorized release record.
