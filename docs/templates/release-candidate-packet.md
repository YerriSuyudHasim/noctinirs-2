# Release-candidate packet template

Copy this file for one candidate. Replace every angle-bracket field.
Delete no evidence-class row. Status is exactly PASS, FAIL, BLOCKED, or
NOT RUN. The deferred-by-decision column is a note, not a fifth status.
A build result is not Class H, and Class I is not marked PASS while its
acceptance criteria are undefined.

This template is not a release, a tag, or merge approval.
v0.1.0 gates are Class S, B, U, and Class I (simulator plus CI `nn2ctl`
conformance). The timestamp contract (ADR-0004 rev 2, #55) and the
simulator conformance test (#56) are in v0.1.0. Real DRDY-latched
timestamp implementation is deferred to v0.1.2/v0.1.3. H-COMMS-1
(#53 / PR #54) is a parked draft and is not a v0.1.0 gate.

| Field | Value |
|---|---|
| Intended version | `<VERSION, must equal PROJECT_VER, development_version, and the tag>` |
| Candidate SHA | `<40-character commit>` |
| Packet status | `<DRY RUN or release candidate>` |
| Board / config | `<target, board revision, sdkconfig defaults, profile>` |
| Build | `<commands, ESP-IDF version and commit, compiler, UTC, run URL>` |

## Included pull requests

| PR | Result SHA |
|---|---|
| `<#N>` | `<40-character merge or squash SHA>` |

## Binaries

| Profile | File | Bytes | SHA-256 |
|---|---|---:|---|
| `<profile>` | `<path>` | `<bytes>` | `<sha256>` |

## Evidence classes

| Class | Status | Deferred by decision | Evidence |
|---|---|---|---|
| S Static | NOT RUN | v0.1.0 gate | `<command, count, URL>` |
| B Build | NOT RUN | v0.1.0 gate | `<run URL, warnings, sizes>` |
| U Host/unit | NOT RUN | v0.1.0 gate | `<count and URL>` |
| I Integration | BLOCKED | v0.1.0 gate. Tracking issue #56. Agent 06 posts criteria on #56 and #50 after the #48/#50 loopback harness is defined. Do not mark PASS before that. | `<procedure, or BLOCKED reason>` |
| H Hardware/on-target | NOT RUN | Deferred for v0.1.0 by Owner directive 2026-10-22 15:25 KST. Keep this row. Flash needs the Owner's direct OK via Nori. No electrical or peripheral claims. | `<separate procedure, or NOT RUN>` |
| R Regression | NOT RUN | Not named as a v0.1.0 gate by the 15:25 KST directive. | `<fixture or suite URL>` |

## Record-only rows

Use this table when a measurement has no budget threshold. Do not force
PASS or FAIL.

| Record | Class | Note |
|---|---|---|
| `<name>` | `<B/S or other>` | `<why this is record-only>` |

## Open risks

- `<risk, including any unmerged dependency>`

## Rollback / recovery

`<how to revert the commit, and device recovery if a flash already happened>`

## Owner decisions required

- Merge approval is Nori jointly with Agent 01. The Owner confirmed that delegation directly to Agent 07 at 15:24 KST on 2026-10-22. Agent approval alone is not merge approval.
- Tags and releases remain with the Owner (M) via Nori.
- `<tag spelling, package attachment, Class H flash authorization>`
