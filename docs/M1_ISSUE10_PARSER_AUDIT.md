# Issue #10 — adversarial CMake parser audit

## Findings and remediation

- **A1 — comment stripping inside quotes:** previous `re.sub('#[^\n]*', '', text)` removed quoted content after `#`; replaced with a tokenizing lexer preserving quoted strings.
- **A2 — register call detection in comments:** previous regex counted `idf_component_register` in comments; lexer now ignores comments.
- **A3 — quoted and multiline dependencies:** scanner now tokenizes literal `REQUIRES` and `PRIV_REQUIRES` values and audits each name against layer allow-list.
- **A4 — dynamic CMake expressions:** `${...}`, generator expressions `$<...>`, semicolon lists, unterminated strings, nested register expressions and selected dependency-mutating commands now **FAIL closed**. Dynamic CMake is not silently accepted.
- **A5 — limited parser scope:** this is a restricted subset, not full CMake evaluation. Further CMake mutation mechanisms and arbitrary command names remain review risks. ADR-0001 still Proposed; Owner/Architect must decide if supported CMake subset is adequate.

## Local verification

- `python3 -m unittest discover -s tests/gate-fixtures -p 'test_*.py' -v`: **21/21 PASS** (13 original + 8 adversarial).
- `bash tools/class-s-gate-selftest.sh`: **PASS**.
- Local tests are not CI evidence. Confirm latest PR head GitHub Actions checks before approval.

## Remaining blockers

- Independent adversarial Reviewer approval; final Class B and reference-integrity evidence at PR head.
- N5 branch-protection decision is Owner-blocked; no settings changed.
- No production inner layers exist yet: Class S production gate NOT APPLICABLE is not conformance evidence.
- No merge, close, tag, hardware or `reference/**` changes authorized.
