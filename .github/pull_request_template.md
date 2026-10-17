<!-- Complete every field. Keep all six evidence rows. Unexecuted = NOT RUN.
Use Refs #N for drafts; never imply issue closure, approval, merge or release.
Follow docs/TRACEABILITY.md and the hub AGENTS.md handoff contract.
This template is a review requirement, not an automatic GitHub enforcement gate. -->

## Identity and scope

- Linked issue URL(s): TODO (use Refs, without an automatic closing keyword)
- Linked milestone URL and name: TODO (or explain why none applies)
- Accountable role / group: TODO
- Author / coauthor identities: TODO
- Branch (`type/issue-id-summary`): TODO
- Base branch and full base SHA: TODO
- **Exact head SHA (40 characters): TODO**
- PR state: Draft; review readiness: TODO
- Problem and resulting behavior: TODO
- Scope, exclusions and dependencies (pin dependency PR + SHA + merge state): TODO

| Changed file | Change and purpose |
|---|---|
| TODO | TODO (list every added, modified, renamed or deleted file) |

## Acceptance criteria

| Issue criterion | PASS / PARTIAL / BLOCKED | Exact-head evidence / remaining work |
|---|---|---|
| TODO | BLOCKED | TODO |

## Evidence classes

For each row select exactly **PASS**, **FAIL**, **NOT RUN**, or
**NOT APPLICABLE**. All rows start NOT RUN. PASS requires actual execution or
the stated static inspection and linked results for the exact head. State the
claim and tested population; split rows when different checks have different
outcomes. NOT APPLICABLE requires a scope-based reason and supplies no evidence.
Keep the hub's per-test BLOCKED result in the detailed handoff when needed;
represent an unexecuted class here as NOT RUN with the blocking reason.

| Class | Status | Claim / procedure / count | Evidence URL and full source SHA / reason |
|---|---|---|---|
| S — Static | NOT RUN | TODO | TODO |
| B — Build | NOT RUN | TODO | TODO |
| U — Host/unit | NOT RUN | TODO | TODO |
| I — Integration | NOT RUN | TODO | TODO |
| H — Hardware/on-target | NOT RUN | TODO | TODO |
| R — Regression | NOT RUN | TODO | TODO |

**Skipped is never PASS.** Absent normative layers: production Class S dependency
gate = NOT APPLICABLE (not conformance evidence). A skipped required gate when
layers exist is FAIL. Fixture or invariant success does not prove production
layer conformance. A green workflow, informational detector or continue-on-error
job cannot replace inspection of individual jobs, steps and test skips.

## CI and reproducibility record

| Workflow / job | Event | Run URL / ID / attempt | Tested source SHA / checkout mode | Raw conclusion | Evidence status / limitation |
|---|---|---|---|---|---|
| TODO | TODO | TODO | TODO | TODO | NOT RUN |

- Commands/procedure, UTC, executor and environment/tool versions: TODO
- Firmware changes: SDK/compiler/target/board/config, warnings and binary size: TODO
- Artifact URLs, retention/expiry and external-file SHA-256/version IDs: TODO
- Reruns / earlier failures / superseded heads (preserve links): TODO
- Tests not run, skips, blockers and residual risks: TODO
- Non-claims (hardware, timing, safety, clinical, integration, reproducibility): TODO

## Reference integrity

- `reference/**` declaration: TODO; confirm no edit, rename, deletion, formatting
  or manifest refresh. Any intentional exception needs separate explicit Owner
  authorization, scope/provenance and independent review under AGENTS.md.
- Before and after: `pwsh -File tools/verify-reference.ps1`; result, UTC, SHA and
  evidence links: TODO
- Diff inventory against base (including manifest) and CI checkout scope: TODO

## Independent review and Owner decision

- Required independent role(s): TODO
- Named reviewer assignment / role handoff link: **PENDING** (identify a
  non-author; a role handoff is not a completed GitHub review request)
- Independence declaration (not author/coauthor; identify role even if sharing
  the GitHub account): TODO
- Reviewed full SHA and review URL: **PENDING**
- Independent decision / blocking findings: **PENDING — no approval claimed**
- Re-review after new commits: **REQUIRED**, unless reviewer explicitly records
  the immaterial-diff assessment on the new SHA; old approval is not transferable
- Owner authorization for preparation: TODO (scope / durable record)
- Owner Accept / Reject / Amend decision URL, PR and full SHA: **PENDING**
- Owner merge authorization: **NOT GRANTED** (record separately after valid checks
  and independent review; preparation authorization is not merge authorization)
- Actual merge SHA / method / actor / UTC: **UNMERGED**

## Rollback and handoff

- Rollback/recovery procedure and risks (or scoped reason none): TODO
- Remaining Owner decisions and single next action: TODO
- Append a complete hub `handoff_version: 1` record to the issue or PR with
  exact SHA, every changed path, criteria, tests, risks, assumptions, open
  questions, recipient, requester and actual UTC. Handoffs and corrections are
  append-only; accepting a handoff does not approve or authorize merge.
