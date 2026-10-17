# Issue #10 — adversarial CMake parser audit

## Initial parser audit at `4855f8fff578c1c70de20cf73bd8cb93e00fbdf8`

- **A1 — comment stripping inside quotes:** previous `re.sub('#[^\n]*', '', text)` removed quoted content after `#`; replaced with a tokenizing lexer preserving quoted strings.
- **A2 — register call detection in comments:** previous regex counted `idf_component_register` in comments; lexer now ignores comments.
- **A3 — quoted and multiline dependencies:** scanner now tokenizes literal `REQUIRES` and `PRIV_REQUIRES` values and audits each name against layer allow-list.
- **A4 — dynamic CMake expressions:** `${...}`, generator expressions `$<...>`, semicolon lists, unterminated strings, nested register expressions and selected dependency-mutating commands now **FAIL closed**. Dynamic CMake is not silently accepted.
- **A5 — limited parser scope:** this is a restricted subset, not full CMake evaluation. Further CMake mutation mechanisms and arbitrary command names remain review risks. ADR-0001 still Proposed; Owner/Architect must decide if supported CMake subset is adequate.

## Initial local verification (historical)

- `python3 -m unittest discover -s tests/gate-fixtures -p 'test_*.py' -v`: **21/21 PASS** (13 original + 8 adversarial).
- `bash tools/class-s-gate-selftest.sh`: **PASS**.
- Local tests are not CI evidence. Confirm latest PR head GitHub Actions checks before approval.

## Fresh continuation audit (2026-10-17)

This is an author-side adversarial code audit and remediation, **not independent Reviewer approval**. Incoming PR head and fetched remote branch both equaled `4855f8fff578c1c70de20cf73bd8cb93e00fbdf8`; no intervening contributor commit was present on this branch. Other branches had advanced and were not incorporated.

| Finding / root cause | Minimal reproduction / regression fixture | Fix |
|---|---|---|
| A6: quoted keywords ignored; token kind used instead of CMake argument value | `idf_component_register(SRCS "a.c" "REQUIRES" esp_wifi)`; `test_n2_quoted_keywords` | Recognize exact keyword values, quoted or plain. CMake 4.0.3 probe confirmed quoted REQUIRES/PRIV_REQUIRES parsing. |
| A7: command deny-list incomplete | `idf_component_set_property(nn2_core REQUIRES esp_wifi APPEND)` after a legal registration; `test_n2_alternative_mutations` | Permit only one literal top-level registration; reject every extra command, including macros, cmake_language, mutation, and control flow. |
| A8: only the selected call was parsed; trailing/duplicate/malformed tokens escaped validation | `idf_component_register(SRCS "a.c"))`; malformed/duplicate and nested-CMake fixtures | Validate the whole token stream; reject nested parentheses, duplicate keywords/registrations and nested CMakeLists. |
| A9: source paths outside scanned tree permitted | `idf_component_register(SRCS ../../outside.c)`; `test_n2_external_source_paths` | Reject absolute, drive-prefixed and parent-traversing registration paths. |
| A10: incomplete include token handling and prefix checks | `#include /*note*/ <esp_wifi.h>`, line-spliced includes, `vendor/driver/gpio.h`; lexical, prefixed and relative cross-layer fixtures | Splice lines, lex comments while preserving strings/header tokens, check path suffixes and layer segments. `driver//gpio.h` remains a header token. Ignore commented examples. |
| A11: pathlib recursive glob can suppress traversal errors; root validation incomplete | Injected PermissionError; invalid UTF-8; components replaced by a file or missing scan root | Explicit scandir traversal, validate hierarchy, catch errors as exit 1, reject links/junctions and special files. |
| A12: workflow filtered real directories, permitting invalid paths to skip the gate | Broken normative link, normative file, symlinked components, unknown file/link; `WorkflowDetectionFixtures` executes actual workflow shell | Detect all matching entries, reject symlink/file hierarchy, propagate enumeration failures. Keep diagnostics in gate step summary. |
| A13: alternate source suffixes omitted | Registered `hidden.cc/.cxx/.S` or `.hh/.hxx` containing `esp_wifi.h`; `test_n1_alternative_source_extensions` | Scan common C/C++ header/source and preprocessed assembler suffixes, case-insensitively; this does not authorize non-C11 firmware. |
| A14: Component Manager can add dependencies separately | `idf_component.yml` with a dependency; `test_n2_component_manager_manifest_fails_closed` | Reject layer-local Component Manager manifests as unsupported; do not silently ignore them. |
| A15: bracket comments misread as code; cache/escape syntax incompletely rejected | Bracket-comment positive, bracket-argument negative, expression/escape variants | Lex line and bracket comments; reject bracket arguments, all `$` expressions, lists and escapes. |

Regression-first evidence: before fixes, the expanded 40-method suite against the incoming parser had 28 failing subcases and one injected-error test setup error (the old module had no `os` import). Workflow detection had five failing subcases in six methods. Alternate extensions had five failing subcases. Existing symlink cases continued to pass; new cases strengthen coverage rather than imply those already failed. These local RED logs are diagnostic, not durable CI evidence.

## Supported subset and residual risks

- One root `idf_component_register(...)` per layer, ordinary or uppercase command spelling; literal paths, quoted/plain dependency names, multiline arguments, line/bracket comments. Keywords are exact uppercase CMake values.
- Extra commands, nested scripts/registrations, duplicate keywords, variables/environment/cache expansion, generator expressions, semicolon lists, escapes, bracket arguments, dependency manifests, and external paths fail closed. Some valid CMake programs are deliberately rejected as unsupported. Extending the subset needs fixtures and review; it must not quietly become a general evaluator. In particular, any later Issue #11 C11 flag commands need explicit supported-subset review.
- No evaluation of project-level CMake, overridden ESP-IDF functions, implicit/toolchain/link-time dependencies, or external build-system mutation. The source include lexer handles the tested C11 literal forms, not full C/C++ preprocessing. A scan over the recognized suffixes does not prove every possible input language or filename is covered.
- G3 macro-expanded, G4 transitive, G5 generated-header dependencies remain deferred. `<stdatomic.h>` remains manual RTOS review. Core's complete standard-header whitelist is not enforced by this forbidden-pattern scanner.
- The ADR remains Proposed. Layer paths and allow-lists are unchanged. N6 remains with Issue #11; N5 remains Owner-blocked. No independent approval is inferred from this audit.

## Continuation local verification

Commands: `python -m unittest discover -s tests/gate-fixtures -p 'test_*.py' -v`, `bash tools/class-s-gate-selftest.sh`, `./tools/verify-reference.ps1`, `git diff --check`.

Environment: Windows, Python 3.14.6, Git Bash 5.3.15, CMake 4.0.3 probe. The Python fixtures invoke their current interpreter directly; the Bash wrapper is still exercised by the legacy suite. A temporary exported Bash `python3` function maps existing path arguments through `cygpath -w` and calls installed `python`; no Python dependency or machine setting was added. The initial unadapted run failed because Windows' python3 alias opened the Store stub (exit 49); this was an environment failure, not a passing gate result.

Executed final suite: **51/51 test methods PASS**, including 69 forbidden-header matrix subcases and six actual workflow detection methods. Bash suite: 19 verdicts PASS locally, with POSIX chmod test explicitly skipped on native Windows; it remains enabled on non-root Linux CI. The injected PermissionError fixture covers scanner exception handling locally. Reference integrity: 319 files, verified before and after changes; `git diff --check` PASS. The exact implementation SHA and executed results are recorded in the append-only PR handoff after commit, with fresh GitHub Actions evidence.

## Incoming-head CI evidence (not evidence for subsequent changes)

All following jobs completed successfully on **`4855f8fff578c1c70de20cf73bd8cb93e00fbdf8`**, PR event, attempt 1; logs inspected on 2026-10-17:

| Job | Conclusion / evidence |
|---|---|
| layer-detect | [success, informational](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39006040921/job/117318619992) |
| class-s-gate-selftest | [success: 21 Python tests + Bash](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39006040921/job/117318620165) |
| class-s-gate-invariant | [success; present=false, gate=skipped](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39006040921/job/117318652010) |
| class-b-esp-idf-build | [success](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39006040921/job/117318620208) |
| reference-integrity | [success: 319 files](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39006040963/job/117318620088) |

Class S production gate was **skipped / NOT APPLICABLE**, not architectural conformance evidence. Class B logs identify ESP-IDF v6.1 @ `fff9895c82d744c7237be8847347bdd1b07c6643`, ESP32-S3, GCC 15.2.0 (`esp-15.2.0_20251204`), CMake 4.0.3, Python 3.12.3; total image 169928 bytes, zero compiler warning lines. [Build artifact](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39006040921/artifacts/12090313788) retains configuration and hashes for 90 days. Board/fixture: not applicable to build-only evidence. Class H: **NOT RUN**.

Rollback: revert the continuation implementation commit on this working branch through review. No firmware/configuration/reference files or hardware state changed. Residual risk is conservative rejection of unsupported syntax and incomplete language/build-system interpretation; independent review must assess those limits.

## Remaining blockers

- Independent adversarial Reviewer approval; final Class B and reference-integrity evidence at PR head.
- N5 branch-protection decision is Owner-blocked; no settings changed.
- No production inner layers exist yet: Class S production gate NOT APPLICABLE is not conformance evidence.
- No merge, close, tag, hardware or `reference/**` changes authorized.
