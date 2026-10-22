# Release-candidate packet template

Copy this file for one candidate. Replace every angle-bracket field.
Delete no row. Status is exactly PASS, FAIL, BLOCKED, or NOT RUN.
Class H stays NOT RUN unless a separate Owner-authorized hardware
procedure recorded that result. A build result is not Class H.

This template is not a release, a tag, or merge approval.

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

## Test matrix

| Class | Status | Evidence |
|---|---|---|
| S Static | NOT RUN | `<command, count, URL>` |
| B Build | NOT RUN | `<run URL, warnings, sizes>` |
| U Host/unit | NOT RUN | `<count and URL>` |
| I Integration | NOT RUN | `<procedure or reason>` |
| H Hardware/on-target | NOT RUN | Leave NOT RUN. Build success does not fill this row. |
| R Regression | NOT RUN | `<fixture or suite URL>` |

## Open risks

- `<risk, including any unmerged dependency>`

## Rollback / recovery

`<how to revert the commit, and device recovery if a flash already happened>`

## Owner decisions required

- Recorded human Owner merge approval, after CI and independent review. Agent approval is not merge approval.
- `<tag name, release publish, package attachment, Class H authorization>`
