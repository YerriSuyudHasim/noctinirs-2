# Integration and evidence traceability

This is the product convention for [Issue #14](https://github.com/YerriSuyudHasim/noctinirs-2/issues/14).
It implements, without replacing, [product AGENTS.md](../AGENTS.md),
[project.yaml](../project.yaml), the
[hub policy](https://github.com/YerriSuyudHasim/ai-engineering-hub/blob/main/AGENTS.md)
(lifecycle §8, evidence §9, handoff §10), and the
[Owner M0 addendum](https://github.com/YerriSuyudHasim/noctinirs-2/issues/8#issuecomment-6208383677).
Use the stricter applicable rule. This document does not grant merge authority,
accept an ADR, select hardware limits, or change repository settings.

## Required chain

```text
Issue → Branch → PR → Commit SHA → CI run → Independent Review → Owner Decision → Merge SHA
```

Each link must resolve to the same change and exact revision. Unknown stages
remain explicitly pending; a Draft may have a useful partial chain.

| Stage | Required durable evidence |
|---|---|
| Issue | URL, milestone URL/name, problem, scope/exclusions, testable criteria, accountable role, risks, dependencies, required specialist reviews and hardware needs |
| Branch | Repository, `type/issue-id-summary`, creation base at current main and issue reference; record later rebases/base changes separately; isolate work, never push to main |
| PR | URL linked back to issue; base/head branches, Draft state, full changed-file inventory, criteria matrix, evidence table, limitations/non-claims, rollback and handoff; use the [template](../.github/pull_request_template.md) |
| Commit SHA | Full 40-character PR head; issue-referencing commits; pin all source, comparisons, reports, tests and reviews to it. Base SHA and dependency SHAs are separate fields |
| CI run | Workflow/job URLs, run ID and attempt, event, source SHA, actual checkout mode, raw job/step conclusions, commands, UTC, executor, tool versions/environment, counts/skips, artifacts/checksums and retention. Record failed and superseded runs as well as current results |
| Independent Review | Non-author/non-coauthor identity and accountable role, assignment/handoff, full reviewed SHA, review URL, findings and explicit decision. Specialist concurrence where applicable. A request or accepted handoff is not approval |
| Owner Decision | Durable issue/PR decision by the human Owner: Accept/Reject/Amend, PR URL, exact reviewed SHA, scope, conditions and authorization. Merge authorization follows valid checks and independent review; it is separate from permission to prepare a Draft |
| Merge SHA | Only after an actual authorized merge: verified merged state, full merge/result SHA, method, actor, UTC and parent/head correspondence. Link from PR and issue with evidence and remaining follow-ups. Merge is not release or deployment |

Do not copy GitHub's `merge_commit_sha` into the final stage for an open PR: it
may identify a synthetic test merge. Verify `merged=true` and the resulting
main history. For squash/rebase, retain the mapping from reviewed head to
resulting commit(s); do not rename the reviewed SHA as the merge SHA.

## Evidence status and scope

S = static inspection/analysis, B = compile/link build, U = host/unit,
I = integration, H = hardware/on-target, R = regression. These are the project
letters already used in the M0 handoff. They describe claims rather than
interchangeable assurances; R can qualify a scoped S/U/B/I/H regression suite.
For example, host reporting-tool tests can be U/R for that tool, without
establishing product Core/Application behavior. Identify simulation and
field/production evidence explicitly under the hub taxonomy; do not silently
relabel simulations as hardware, integration or field results.

All six template rows default to **NOT RUN**. Explicit statuses are mandatory at
review handoff; TODO fields are incomplete. The template is a checklist reviewed
by a person, not a parser or enforced CI gate. Allowed class-table statuses:

- **PASS:** the stated check actually ran (or the identified static inspection
  occurred), criteria passed and exact-head evidence is available. Limit the
  claim to the checked population; record test counts and all skips.
- **FAIL:** executed check failed, tool/scanner error occurred, or a required
  gate was skipped despite its applicable preconditions. Retain the failure.
- **NOT RUN:** not executed, blocked, cancelled, awaiting equipment/authorization
  or still pending; state the reason. A queued run has no passing evidence.
- **NOT APPLICABLE:** a documented scope/precondition makes the check inapplicable;
  this supplies no evidence and is never green/pass.

The hub's per-test/handoff vocabulary additionally permits **BLOCKED**; retain
that result and reason there. The class table uses NOT RUN for an unexecuted
blocked test. Acceptance matrices use PASS / PARTIAL / BLOCKED to describe
completion, not to disguise test failures. Always include the raw result too.

The absent-production-layer gate is NOT APPLICABLE. Passing fixtures verify the
gate tool; passing `layer-detect` or `class-s-gate-invariant` alone does not prove
layer conformance. If layers exist, their required gate must run and pass.
Inspect per-step outcomes in informational/continue-on-error jobs: workflow
success alone is insufficient. Split mixed results into separate claim rows.

Every evidence record carries repository, full SHA, command/procedure,
tool/version/configuration/environment, UTC, executor, result, limitations and
artifact URL; physical work also needs board/fixture identifiers. File evidence
outside Git needs SHA-256 and controlled storage/version where applicable.
Use immutable source links, retained CI artifacts or controlled versioned storage;
editable local logs are provisional. Product retention is at least 90 days where
supported; arrange approved preservation before expiry, keep original run links
and checksums, and do not weaken retention without a documented alternative.

## Drafts, revisions and related work

| Situation | Required handling |
|---|---|
| Draft PR | Link with `Refs #N`, retain Draft state and every pending review/Owner/merge field. A REVIEW READY handoff means ready for independent inspection only; neither Draft CI nor author verification approves the change |
| CI rerun | Keep the original run/attempt and failure. Append run ID + attempt + job/artifact links, same source SHA and reason for rerun. Do not overwrite history or imply a rerun fixed source. A different source SHA is a new revision |
| New commit | Mark prior CI/review/Owner evidence historical for the old SHA. Refresh the inventory, evidence and handoff, rerun applicable CI and obtain review of the new SHA. Material changes require repeat independent review; immaterial changes require an explicit reviewer assessment on the new SHA. Never transfer old Owner merge authorization implicitly |
| Multiple PRs for one issue | Maintain an issue-level list of all PRs, roles, scopes, heads, states and dependencies. Keep separate chains and criteria coverage for each. Closing/superseding one PR does not complete the issue; closeout must account for all required deliverables |
| Unmerged dependency | Pin dependency PR + SHA, distinguish reference-only from incorporated/stacked code, state overlaps and unresolved compatibility. Do not call it main or accepted. Record real base lineage. Separate-branch green runs do not establish combined compatibility; authorized integration requires fresh combined-SHA checks and review |
| Rejected or amended PR | Preserve the PR, failed evidence, reviewer findings and Owner decision. Append the reason and successor/revision links. Rejected work is not accepted history. Amendments get new SHA/evidence/review/Owner decision; do not erase or silently rewrite prior handoffs |
| Missing hardware evidence | H stays NOT RUN with equipment/procedure/authorization blocker and the exact unsupported claims. Block criteria needing it and hand off to the Owner/specialist. Continue independently testable documentation work; build/host/simulation evidence cannot fill the hardware gap |

Hardware timing, electrical/optical safety, peak runtime memory and clinical
behavior need their applicable evidence and authorization. This documentation
does not authorize flashing, energized tests or device control.

## Review, handoff and Owner gates

Post a complete `handoff_version: 1` record from hub §10 to the issue or PR.
Include every required field: project/repository/issue, from/to role, SHA,
status, summary, changed paths, criteria/tests with evidence, risks, assumptions,
open questions, next action, requester and actual UTC. Preserve entries and
append corrections referencing the earlier record. The recipient verifies SHA,
scope, evidence and risks before accepting responsibility.

The author must not act as independent reviewer. If roles share a GitHub login,
record the separate agent/person role and review provenance explicitly;
`COMMENT`/concurrence is distinct from a GitHub approving review. The Owner
addendum recognizes this shared-account limit. A pending assignment must say
PENDING; do not invent a person, review request or decision. The Owner records
the eventual assignment when none is named. A fresh non-author Reviewer must
check this Issue #14 change; its author-side checks are not independent review.

`reference/**` remains read-only. Record integrity checks before/after work and
the exact diff against base, including the manifest. Unexpected mismatches stop
the affected work. An intentional reference-maintenance exception requires
separate explicit authorization and review under AGENTS.md.

At the inspected main, firmware CI explicitly checks out the PR head;
reference-integrity CI uses the default PR checkout (synthetic merge tree).
Record that distinction: a PR run's API `head_sha` alone does not prove that its
checkout tested that head. Use exact-head push integrity evidence as well.
Do not import the unmerged #40 checkout fix or imply it is already on main.

## Retrospective application and maintenance

The [PR #41 traceability example](evidence/issue14-pr41-traceability.md) applies
this convention to a real existing M1 Draft without changing its branch or
claiming approval. It is a timestamped historical record; a later head needs a
new linked record. The [changelog](../CHANGELOG.md) separates merged M0/main
history from pending M1 candidates.

The convention is an author/reviewer requirement. GitHub does not enforce these
Markdown fields automatically; this change adds no settings or required-check
changes. Use existing CI plus scoped static validation for this documentation;
no new test framework or workflow is needed for a Markdown-only change.
