# PR #38 F1/F2 remediation — author-side evidence

Scope: Issue #10 N1 only. This is implementation and author-side Verification,
not an independent review or approval. The independent report returned REQUEST
CHANGES at `71bd7ae4413ce06cf12d24b384ebac554e3a647a`.

## Intake and unchanged-head reproduction

The live PR metadata, branch and pull ref matched that SHA before editing; the
worktree was clean. Current fetched hub/product AGENTS policies were read (hub
main `4d8772b2fb28891dc7bd7de5164b19c8b8835cfa`, product main
`4a21171353b72ceff62774ba114705e0c8be2c31`); their AGENTS content matched the
local copies. No nested product AGENTS was present. Reference integrity verified
319 files before work.

The complete supplied report and ZIP contents were inspected, including the
helper, JSON results, original logs, provenance, checksums and limitations. All
eight ZIP member checksums match SHA256SUMS.txt. Attachment SHA-256:

- Report: `c8b6625cf07074cc2cb0b25153d5d685fb13ee19b44ba432c9bc3bcecc086475`.
- ZIP: `f4bb492bff2d643a9f13a84f5f686b7f6c024e5de7d0106749f13e81579b07a4`.
- Reviewer helper: `13707f8ab4fed63618fc744743ac688a414183a222aea9a2ffd3cf77c766522b`.

Attachment instructions were treated as review evidence, not as authorization to
change another PR, accept the ADR, or approve this implementation.

Fresh reproduction ran at 2026-10-17T15:45:27Z on Windows 11 10.0.26200,
Python 3.14.6, using temporary three-layer fixtures with legal registrations.
Each source was replaced, rather than appended, so the BOM was byte zero.
The stub `esp_wifi.h` contained `typedef int ESP_WIFI_FORBIDDEN_MARKER;`.

| Fixture bytes (UTF-8) | Layers | Original scanner | Compiler experiment |
|---|---|---|---|
| `EF BB BF` then `#include <esp_wifi.h>` and LF | Core, Ports, Application | exit 0 / PASS, defect | exit 0; stub marker present |
| `??=include <esp_wifi.h>` and LF | Core, Ports, Application | exit 0 / PASS, defect | exit 0; stub marker present |
| `#inc??/` then LF then `lude <esp_wifi.h>` and LF | Core, Ports, Application | exit 0 / PASS, defect | exit 0; stub marker present |

Compiler: `xtensa-esp32s3-elf-gcc.exe`, GCC 15.2.0,
crosstool-NG `esp-15.2.0_20251204`, installed Windows cross compiler.
Command: `<compiler> -std=c11 -pedantic-errors -E -I <fixture-root> <source.c>`.
All nine preprocessing runs succeeded and consumed the marker. These are
preprocessing experiments on synthetic input, not a target build or ESP-IDF
header validation. With `-E -I <fixture-root> <source.c>` and no language flag,
the BOM include was consumed, while both trigraph forms were ignored (marker
absent). This corroborates the report's language-mode boundary using a different
compiler environment; it does not claim GCC 13.3.0 was locally rerun. ADR rev1's
C11 contract makes F2 applicable despite default-mode behavior. Neither finding
conflicts with the contract or falls within accepted R1/R2.

## Root causes and minimal correction

- F1: UTF-8 decoding retains U+FEFF, which is not regex whitespace. The anchored
  directive matcher therefore misses the first include. Remove exactly one
  optional leading BOM before lexing, including direct calls to literal_includes.
- F2: the lexer spliced ordinary backslashes but omitted C11 phase-1 trigraph
  replacement. Apply the nine C11 replacements in one pass before the existing
  line-splice/comment/header lexer. No macro evaluation or general preprocessor
  is introduced.
- Keep the original spelling scan when translation changes text. The scanner
  covers suffixes whose compiler modes may ignore trigraphs; a newly formed
  backslash must not extend a `//` comment and hide an ordinary include in those
  modes. Either view's parse error still fails closed. This is conservative:
  mode-dependent text can be rejected even if one selected mode would accept
  it or comment it out. Selecting compiler semantics from build flags remains
  outside this bounded scanner.

The existing lexer is moved into unspliced_literal_includes without changes to
its splicing/comment/header behavior. Layer maps, forbidden patterns, CMake
parser, wrapper and workflows are unchanged.

## Regression-first evidence (failed and successful runs are distinct)

`tests/gate-fixtures/test_issue10_lexical.py` adds eight methods and 96 scanner
subcases, each isolated across all three layers. It covers BOM byte-zero
forbidden/allowed quoted and angle headers; trigraph hash, LF/CRLF splice,
combined spellings, interposed comments, header splices, comment-creation and
continuation controls, inactive-trigraph mode, and malformed-header fail-closed
behavior. Legal `<stdint.h>`/`"stdint.h"` controls must pass.

| Run | Result and boundary |
|---|---|
| Original suite, unchanged reviewed HEAD | 51 methods PASS; Bash 19 verdicts PASS, one explicit native-Windows chmod SKIP |
| Initial seven-method new suite, unchanged scanner | FAIL: 42 failing subcases; retained separately, before source correction |
| Final eight-method new suite against exact original scanner extracted with git show | FAIL: 45 failing subcases (6 BOM, 30 trigraph directives, 6 splice/header cases, 3 malformed headers); other controls passed |
| Final eight-method suite against corrected working tree | PASS: 8 methods / 96 subcases |
| All original plus new methods against corrected working tree | PASS: 59 methods |
| Separate nine-case compiler/scanner reproduction after correction | Scanner exit 1 in all nine; strict C11 still consumes stub in all nine |
| Existing Bash suite after correction | PASS: 19 verdicts; native Windows chmod SKIP remains explicit |
| Reference integrity after correction / diff check | PASS: 319 files / no whitespace errors |

The final original-scanner test procedure never rewrites the working scanner:
extract `git show 71bd7ae4413ce06cf12d24b384ebac554e3a647a:tools/class-s-gate.py`
as bytes into a temporary file; import test_issue10_gate and test_issue10_lexical
with tests/gate-fixtures on sys.path; set test_issue10_gate.GATE to that file;
load only test_issue10_lexical through unittest and execute it. The nonzero
result is expected defect evidence, not a passing test run.

Commands for corrected suites:

```sh
python -m unittest discover -s tests/gate-fixtures -p test_issue10_lexical.py -v
python -m unittest discover -s tests/gate-fixtures -p 'test_*.py' -v
bash tools/class-s-gate-selftest.sh
```

Also run `./tools/verify-reference.ps1` and `git diff --check`. Local Bash
5.3.15 uses the existing process-local cygpath/Python adapter, not a repository
or machine configuration change. The injected PermissionError test prints an
expected scanner FAIL while its assertion passes. Local logs are diagnostic;
the final committed-SHA reruns and immutable CI job/artifact links belong in
the final author handoff. Existing CI already discovers this new test file;
no workflow adjustment is needed.

Retained diagnostic SHA-256 (outside the repository):

| File | SHA-256 |
|---|---|
| reproduce_lexical.py | `536ba294b2220226ab4847fea52db6b0d3e715cbfe755fff0fa16643c7c1fa2f` |
| reproduction-before.json | `a1335dcac74f5e1ceb3f1d5d3e14b0b43dc7c4a4531cc8a34aa58e016bf44093` |
| regression_against_original.py | `7e71456b4f61436326c090d688ac14c45e2ecb88c77a850aba5f820060e95bd9` |
| final-regressions-original-red.log | `5b84cdbbf0a75962cd07c4cf4eab3d139c19b8eabdb52727a002cba9ba461417` |

The first two bind to the unchanged reviewed SHA; the final RED procedure binds
the final test file to that exact original scanner. No local log is represented
as a read-only CI artifact. The reviewer-owned full 132-case Linux suite was
NOT RUN in this native Windows remediation; its supplied 123 PASS / 9 FAIL
result is historical reviewer evidence only. Its unmodified helper requires
Linux FIFO/UID facilities; no Windows adaptation is claimed equivalent.

## Governance, compatibility and independent handoff

Owner R1/R2 remain ACCEPTED WITH CONDITIONS under
[the recorded decision](https://github.com/YerriSuyudHasim/noctinirs-2/pull/38#issuecomment-6233707924):
preserve limitations; production NOT APPLICABLE is not conformance; reassess
before unsupported production mechanisms; preserve #43 manifest compatibility;
keep ADR-0001 Proposed. Macro/transitive/generated/root/toolchain/build/link
coverage and the complete Core standard-header whitelist remain manual/deferred;
stdatomic still requires manual RTOS review. N5 configuration stays Owner-blocked.

PR #43 remains pinned at `e65e6596b32d9fdc6d779cddfd6fa583f3031efc`, using old #38
input `71bd7ae4413ce06cf12d24b384ebac554e3a647a` and #39 input
`789b4fa8bb2904a85116c6c6116973c348565c05`. The new #38 SHA supersedes that input;
old combined green checks cannot validate the new combination. This lexical
fix changes no manifest policy/API, but an integration engineer must refresh
the candidate with the actual new #38 SHA, preserve #38 -> #39 -> #43 ordering,
reconcile consumers without losing normalization, and rerun original, new,
manifest/C11/integration, Class B and reference checks at the actual combined
SHA with required independent review and Owner decisions. No #43 modification
or refreshed-combination PASS is claimed here.

Independent reviewer: please re-review F1/F2, translation ordering, conservative
dual-mode scanning, positive controls and exact-final-SHA CI. Reproduce the
new tests against both the original scanner and corrected SHA, and decide
whether further adversarial coverage (including your full suite) is needed.
The executor remains implementation author and cannot independently approve.
Formal reviewer identity and a truthful non-author recording route remain
necessary; this document is not such approval.

Changed paths in this remediation: tools/class-s-gate.py,
tests/gate-fixtures/test_issue10_lexical.py and this report. No main, reference,
settings, hardware, ADR status, #43, merge or Issue #10 closure changes.
Rollback is a reviewed revert of the remediation commit on #38's existing
branch with fresh checks; there is no deployed firmware state to roll back.
Production Class S remains SKIPPED / NOT APPLICABLE, not conformance evidence.
Hardware/on-target/HIL and fresh local full target build are NOT RUN. Final
CI is a separate Class B compile/link claim, never physical validation.
