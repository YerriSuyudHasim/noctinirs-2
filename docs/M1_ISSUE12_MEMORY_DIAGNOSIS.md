# Issue #12: memory diagnosis and regression framework

Status: prepared by Group 1 RTOS Specialist (lead) / Firmware Developer (support).
Budget **PROPOSED, not accepted**. Independent Group 2 Hardware Specialist review and
Owner merge approval remain pending. Evidence is static Class B plus host tests of
the reporting tools. No configuration optimization or firmware change is included.

## Scope, provenance and related work

Branch: `diagnostics/12-memory-budget-regression`, created from current main
`4a21171353b72ceff62774ba114705e0c8be2c31`. This exact SHA is the regression baseline.
The issue's older main `9f925bf85eabf2d718d03b6153d5132451eeda84` is contextual;
the original M0 build source is `cd4ed7f88905a1c430e0b942732ddc0613081aef`.
These identities are not interchangeable.

Discovery covered root AGENTS, project registration, hub AGENTS at
`4d8772b2fb28891dc7bd7de5164b19c8b8835cfa`, ADR-0001/0002/0003, all Issue #12
comments, current main/open PRs and existing PR #37 memory reports. PR #37
already captures observability and a provisional budget; this isolated PR adds
validated reusable comparison tooling and tests. It neither updates nor supersedes
its independent reviews. Future integration must reconcile overlapping scope.

[PR #40](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40) remains open at
`f97767035e0ab4f801e655ec751141314668c407`. Its reproducible-build configuration,
two-clean-build runner and binary comparator are **not incorporated**. Its recorded
SDK/compiler identities agree with the pin already on main:

- ESP-IDF v6.1 commit `fff9895c82d744c7237be8847347bdd1b07c6643`.
- GCC `xtensa-esp-elf-gcc (crosstool-NG esp-15.2.0_20251204) 15.2.0`.
- Existing OCI reference `espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.
- Local measurement: Windows x86_64, SDK `C:/esp/v6.1/esp-idf`, Python 3.14.6,
  CMake 4.0.3, Ninja 1.12.1, esp-idf-size 2.3.2. SDK tracked tree clean.
- CI: Linux amd64/glibc 2.39 in the existing pinned image; Python 3.12.3,
  CMake 4.0.3, Ninja 1.11.1, esp-idf-size 2.3.1. Each collected `metrics.json`
  records actual Python/platform, size-tool version, compiler, SDK, configuration
  hashes, UTC, source SHA and run URL; raw tool versions and command exits are retained.
- Target `esp32s3`; firmware version 0.1.0; board/fixture not applicable to a host build.

Local initial build commands, at the exact baseline before committing tooling:

```powershell
$env:PYTHONUTF8 = '1'
. C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1
idf.py set-target esp32s3
idf.py build
python tools/collect_memory.py --project . --out ../.issue12-local/local-baseline-2 --expected-sha 4a21171353b72ceff62774ba114705e0c8be2c31 --reuse-build
```

The collector rebuilds, then runs **`idf.py size`, `idf.py size-components`,
`idf.py size-files`** through the active Python/SDK, recording every command and
exit code. It additionally runs `python -m esp_idf_size --format json2` with
summary/`--archives`/`--files` against the generated map. CI rebuilds baseline and
PR head from two fresh checkouts, without `--reuse-build`; it is the durable
evidence source. Local rebuilt evidence is labeled as such, not called a second
clean reproducibility proof. No dependency on merging PR #40 is needed for size
analysis; binary identity remains PR #40's scope (timestamps can change hashes).

## Original M0 evidence

Both original artifacts were downloaded and their ZIP SHA-256 checked against
fresh GitHub artifact metadata. Both runs succeeded at source `cd4ed7f88905a1c430e0b942732ddc0613081aef`
using the same SDK/compiler pin, Python 3.12.3, CMake 4.0.3 and Ninja 1.11.1.

| Run | Artifact | Verified ZIP SHA-256 |
|---|---|---|
| [38847664721](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/38847664721) | 12024271866 | `4ed14bf768b6d7a719f4a3e5962e6f0da1c91b7eaafbfb8802cc851f2a93d065` |
| [38847669878](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/38847669878) | 12023717650 | `4cd60aa8fa7198770bf8c6ab00eca4acf818dd96cb44f3f6a6d168a0f21dd767` |

Their archived size reports agree: IRAM 16,384/16,384; DIRAM 48,242/341,760;
DIRAM text 32,935; flash code 69,114; flash data 38,760; image estimate 169,928;
app binary 170,048 bytes. They do not originally include component/file reports;
the new clean baseline run supplies those at **main's own SHA**, not an inference
from a Wi-Fi PR. Original binary hash differences are addressed with downloaded
ELF evidence in PR #40 and do not imply different memory allocations.

## Measured main baseline

Values below are measured from the baseline build, not forecasts. Committed
baseline metrics and the CI artifact supply exact hashes and environment.

| Metric | Used B | Capacity B | Remaining B | Interpretation |
|---|---:|---:|---:|---|
| Fixed IRAM | 16,384 | 16,384 | 0 | 15,356 text + 1,028 vectors; 100% of this memory type |
| DIRAM instruction text | 32,935 | shared | shared | Instruction RAM in the dual-mapped region |
| IRAM placed total | 49,319 | shared | shared | Fixed IRAM including vectors + DIRAM text |
| Static DRAM data | 15,307 | shared | shared | 12,699 data + 2,608 BSS |
| DIRAM aggregate | 48,242 | 341,760 | 293,518 | Text + static data/BSS; 14.12% used |
| Flash code | 69,114 | unavailable | unavailable | Flash-mapped text |
| Flash data | 38,760 | unavailable | unavailable | 38,488 rodata + 256 appdesc + 16 TLS template |
| App binary | 170,048 | 1,048,576 | 878,528 | 16.2170% of generated factory app partition |
| RTC SLOW / FAST | 36 / 24 | 8,192 each | 8,156 / 8,168 | Separate RTC memory types |

Rows describing subcategories overlap region totals; never sum all rows. The
flash image estimate is 169,928 B; the actual `.bin` includes padding/headers and
is 170,048 B. Code/data flash windows are not physical capacities. App headroom
uses the generated partition binary decoded by IDF, not an assumed flash chip.
Generated config selects 2 MiB flash and the SDK single-factory-app table
(`partitions_singleapp.csv`, app offset `0x10000`, 1 MiB). This is a build
configuration observation, not validation of a board's flash or an OTA decision.

## IRAM 100%: evidence and root cause

**High confidence for this exact scaffold, SDK and configuration:** 100% is a
memory-type accounting boundary, not evidence of overall ESP32-S3 RAM exhaustion.
The linker succeeds and places additional instruction code in shared DIRAM.

The pinned SDK's `components/esp_system/ld/esp32s3/memory.ld.in` defines one
`iram0_0_seg` beginning at `0x40370000 + CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE`.
Generated config selects a 16 KiB instruction cache (`0x4000`), so this origin is
`0x40374000`. The size tool's `chip_info/esp32s3.yaml` classifies addresses
`0x40370000..0x40378000` as the fixed IRAM type, and the following instruction
addresses as the secondary alias of DIRAM (`0x3fc88000` data alias).

Actual map evidence:

```text
iram0_0_seg  origin 0x40374000 length 0x57700
dram0_0_seg  origin 0x3fc88000 length 0x53700
.iram0.vectors 0x40374000 size 0x404   (1,028 B)
.iram0.text    0x40374404 size 0xbca3 (48,291 B)
_iram_end      0x40380100
_heap_low_start 0x3fc93cd0
```

Of `.iram0.text`, 15,356 B fits before `0x40378000`; 32,935 B lies after that
boundary. Adding the vectors fills exactly 16,384 B of the fixed type. The same
linker output section crosses the size-tool type boundary; this is not a new
runtime allocator or a fallback taken after a failed link.

`sections.ld.in` deliberately advances `.dram0.dummy` past the shared instruction
allocation using `_iram_end - _diram_i_start`, preventing its data alias from
being allocated twice. Map/alignment/prefetch padding and linker reservations
also exist: **293,518 B is the size tool's static headroom**, not a directly
measured heap value or simply `_heap_end - _heap_low_start`. Boot/startup/cache
reservations and runtime reclamation mean neither region sum is total S3 SRAM
utilization. ROM functions are not counted as newly allocated RAM code.

Actual config also selects 32 KiB data cache, disables PSRAM, leaves
`FREERTOS_IN_IRAM` off, leaves `FREERTOS_PLACE_ISR_FUNCTIONS_INTO_FLASH` off,
keeps heap functions in internal RAM, enables `SPI_MASTER_ISR_IN_IRAM`,
`SPI_FLASH_PLACE_FUNCTIONS_IN_IRAM`, and Wi-Fi IRAM/RX optimizations. Ordinary
FreeRTOS code need not all be in IRAM: required ISR paths/port code still reside
there. Enabled Wi-Fi/SPI defaults do not establish that their full drivers or
application ISRs are linked. `app_main.c` only logs two scaffold messages.

## Measured consumers

Baseline `size-components.json` assigns the following bytes. IRAM placed is
fixed type plus shared instruction text; DIRAM total additionally includes data.
Archive attribution may exclude padding; use the summary for region occupancy.

| Archive | Fixed IRAM B | Shared text B | IRAM placed B | DIRAM total B |
|---|---:|---:|---:|---:|
| libspi_flash.a | 1,430 | 8,468 | 9,898 | 14,403 |
| libesp_hw_support.a | 2,203 | 4,845 | 7,048 | 5,438 |
| libheap.a | 1,187 | 5,477 | 6,664 | 5,490 |
| libfreertos.a | 384 | 5,303 | 5,687 | 9,175 |
| libesp_hal_mspi.a | 2,600 | 2,774 | 5,374 | 2,810 |
| libesp_system.a | 2,684 | 790 | 3,474 | 1,544 |
| libxtensa.a | 2,039 | 457 | 2,496 | 1,517 |
| libmain.a (project) | 0 | 0 | 0 | 0 |

`libmain.a` has 66 B flash code and no attributed RAM. Framework/SDK-linked
support code and padding account for the resident instruction allocation.
This scaffold provides no measured application ISR cost. Largest objects in
`size-files.json` by fixed+shared instructions include `tlsf.c.obj` 5,146 B,
`spi_flash_hal_iram.c.obj` 3,249, `spi_flash_chip_generic.c.obj` 3,002,
`tasks.c.obj` 2,983, `rtc_clk.c.obj` 2,620, and `xtensa_vectors.S.obj` 2,009.

Future ADS DRDY/SPI callbacks, LED/timer ISRs and their transitive callees,
literal pools, constants and data must be checked in each feature's map and
disassembly. `IRAM_ATTR` or a file-level size row alone does not establish
cache-disabled safety. ISR-safe synchronization and internal/DMA-capable
buffers need separate review. No custom ISR exists here to validate; feature
PRs #34/#36 are different source/configuration experiments, not measured
incremental attribution for this baseline.

## Preliminary v0.1.x budget (Owner decision pending)

All bands are advisory. Values at a limit do not alert; exceeding a maximum or
falling below a minimum does. Critical means a planning escalation, not a
production gate. A linker overflow still fails the normal build independently.

| Metric | Baseline | Proposed warning | Proposed critical | Rationale |
|---|---:|---:|---:|---|
| Fixed IRAM | 16,384 B, 100% | no percentage alert | no percentage alert | Full fixed type is normal here; track shared growth |
| IRAM placed, vectors included | 49,319 B | growth >16,384 B (65,703 total) | growth >32,768 B (82,087 total) | Review new ISR/Wi-Fi code; consumes shared headroom |
| Static DRAM | 15,307 B | >65,536 B | >98,304 B | Review buffer/static-stack/data growth; not an independent region capacity |
| Shared DIRAM headroom | 293,518 B | <131,072 B | <98,304 B | Preserve space for runtime allocations; unvalidated floor |
| App partition | 170,048 B / 1 MiB | >70% | >80% | Leave update/feature space; revisit approved partition/OTA design |
| Flash code/data | 69,114 / 38,760 B | track deltas | partition band above | No invented capacity for mapped windows |

The warning headroom floor allows 162,446 B additional static occupancy before
alerting. It cannot promise that much runtime allocation: baseline startup
allocations have not been measured. An illustrative, **unmeasured** future
allocation envelope is ADS131M04 acquisition/DMA/rings 16 KiB; LED/timer state
4 KiB; six named tasks/queues 32 KiB; DSP working buffers 24 KiB; Wi-Fi code,
data and dynamic buffers 64 KiB. Total 140 KiB (143,360 B); relative to the static
baseline this leaves 150,158 B, only 19,086 B above the warning floor. These are
planning allowances, neither measured component consumption nor fixed task
stack sizes. Their static/dynamic splits are undecided. Future ADS1299/dual-rate
work (#31), network mode and DSP algorithm can exceed them; reassess at each
integration. Do not add this envelope again to measured static categories or
assume it is a proven runtime heap reserve.

## Configuration review: proposals only

Expected effects below are SDK documentation statements or bounded hypotheses,
**not A/B measurements**. None is applied. Each requires independent RTOS and
Hardware review, then Owner authorization in its feature/configuration PR.

| Candidate | Expected memory effect | ISR/FreeRTOS/peripheral implications and safety risk | Evidence before adoption |
|---|---|---|---|
| Size compiler optimization instead of current debug mode | Could shrink flash and some resident code; magnitude unknown | Changes instruction timing/debuggability and ISR/task execution; peripheral deadlines could shift | Matched-config map diff, debug assessment, timing/jitter and fault/recovery tests |
| Move heap routines to flash | Could recover part of libheap's 6,664 B resident code; whole-archive total is not a savings prediction | Cache misses or cache-disabled access; `SPI_MASTER_ISR_IN_IRAM` depends on heap staying internal; DMA buffer allocation/callback safety can break | Full Kconfig dependency review, ISR call graph, flash-write coexistence and allocation-failure tests; never toggle solely for size |
| Move SPI-flash operation functions from IRAM | Kconfig says almost 10 KiB depending on linked functions, not measured here | Visible only with auto-suspend support; flash/cache-disabled execution and RTOS stalls affect acquisition and LED deadlines | Supported flash-chip suspend behavior, matched build/map, cache/ISR audit and worst-load timing |
| Disable Wi-Fi IRAM/RX optimizations when Wi-Fi is implemented | Kconfig describes >10 KiB and >17 KiB respectively in suitable Wi-Fi builds; not additive measured savings here | Reduced Wi-Fi throughput and more cache dependence; Core-0 task/interrupt load and frame-loss effects | Linked-feature builds, throughput/heap profiling, acquisition+LED coexistence and loss/jitter evidence |
| Enlarge instruction cache to 32 KiB | Consumes the current 16 KiB fixed IRAM opportunity; moves pressure to shared RAM, not a free-memory fix | May improve cache hit rate but steals SRAM; changes worst-case cache/task/peripheral latency | SDK map arithmetic, matched measurements, concurrent Wi-Fi/ADC/LED timing and heap tests |
| Smaller data cache / future PSRAM use | Smaller cache can release heap-only memory; PSRAM could hold noncritical DSP buffers if hardware supports it; amount unmeasured | Cache latency, DMA accessibility, external-memory stack/ISR restrictions and fragmentation risks; no board capability inferred | Board-approved memory contract, capability-specific allocation tests, complete cache-disabled call/data audit and hardware timing |
| More FreeRTOS ISR functions in flash | Some ISR-path IRAM saving possible only when auto-suspend permits; current option off | Higher interrupt latency/cache dependency, scheduler/queue timing and flash critical sections can break DRDY/LED deadlines | SDK dependency evidence, transitive ISR audit, measured interrupt latency under flash/Wi-Fi load; baseline has no such claim |

Pinned primary sources: SDK `components/{heap,freertos,spi_flash,esp_driver_spi,esp_wifi}/Kconfig`,
`components/esp_system/port/soc/esp32s3/Kconfig.cache`, linker sources cited above,
and size-tool `chip_info/esp32s3.yaml`. Their copies/effective generated output
are available through the pinned SDK/container and collected map/config/scripts.

## Regression tooling and CI usage

No new package is needed: the collector uses ESP-IDF's installed tools and the
comparator/tests use Python standard library. Fresh source tree example, inside
the existing pinned SDK environment:

```bash
python tools/collect_memory.py --project /path/to/clean-checkout --expected-sha FULL_40_CHAR_SHA --out /tmp/new-memory-evidence
python tools/memory_report.py --baseline docs/evidence/issue12-baseline.json --current /tmp/new-memory-evidence/metrics.json --thresholds tools/memory-thresholds.proposed.json --json /tmp/comparison.json --markdown /tmp/comparison.md
python -m unittest discover -s tests/memory -v
```

The collector refuses to overwrite an evidence directory, checks exact source
and SDK SHAs, source cleanliness (including untracked inputs) and compiler,
and fails immediately on command
failure. Output includes all three text and JSON reports, map, ELF/BIN, generated
config/linker scripts, partition CSV/bin, compile commands, project metadata,
command statuses, actual environment and SHA-256 inventory. `failure.json` records
partial attempts; it cannot become a valid baseline. `--reuse-build` is an
explicitly labeled local convenience, not used in CI.

Supported input is **ESP32-S3 esp-idf-size json2 schema 1.2**, default capacity
mode. Unknown regions/sections, missing/inconsistent/duplicate layout, invalid
capacity, malformed JSON, conflicting config values or absent provenance fail
closed. The size tool's zero flash-capacity sentinel is converted to `null`;
zero RAM capacity is invalid. Static DRAM capacity is shared and remains `null`.
New layouts require an explicit parser/test update, not silent omission.

Comparison reports used, capacity, headroom, absolute/percentage used deltas and
headroom deltas. For nonzero growth from zero, percentage is unavailable; two
zero values yield 0%. Different firmware SHAs are expected. SDK, target,
compiler, size-tool version, effective-config hash, inherited build flags or
capacity mismatch yields `NOT_COMPARABLE` with descriptive deltas only. Config
hashing ignores comments/order and tolerates identical deprecated aliases;
raw config hash is retained. Output states are `OK`, `WARNING`, `CRITICAL`,
`NO_BASELINE`, `NOT_COMPARABLE`, `UNAVAILABLE`, `INVALID`; **no absent-data PASS**.
Malformed inputs exit 2 while still writing JSON/Markdown diagnostics. Warning
and critical bands exit 0 because the budget is advisory.

The new workflow `memory-report.yml` builds the fixed baseline and exact event
head in two fresh checkouts on push/PR. It publishes the Markdown step summary
and raw artifacts for 90 days. `memory-tool-tests` checks tool correctness;
`memory-report (informational)` is explicitly nonblocking. Its build/parser
failures stay visible in step logs/failure records. Existing Class B and
reference-integrity workflows are unchanged and retain their own verdicts.
Preserve artifacts in approved storage before expiry if longer retention is needed.

## Validation, handoff and residual limitations

Initial implementation CI at `df3ca7f1958c7f5c2052d8dcae5625cc9a4f3cbf`:

| Check | Result | Run |
|---|---|---|
| Memory diagnostics: baseline + head clean builds, comparison | PASS, `OK`, every used delta 0 B | [39020072707](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020072707) |
| Host tool tests (initial 30 tests, before final added empty-image test) | PASS, no skips | same run |
| Existing Class B, Class S selftest/invariant | PASS; dependency gate NOT APPLICABLE | [39020072642](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020072642) |
| Reference integrity | PASS, 319 files | [39020072708](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020072708) |

[Memory artifact 12095838823](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/39020072707/artifacts/12095838823)
ZIP SHA-256 `babae8369c7840cf196bbb3356cf974b1df4f70b900b3a89b16e446937103476`
matches the GitHub API digest. All 33 files in each of the baseline/current
inventories were rehashed after download (66 verified files). All recorded
commands exited 0; both collection modes are `clean-build`. Both builds contain
zero `warning:` lines. Baseline and head generated effective-config hashes match:
`6585b44741b9d89db087604c7fdb670350a7526eb821ff9dc961804f386c0c00`.
The raw config hash is `5c6b7e1073053eb0580faae611d4736d6d72424934035b865166f9659d0c626f`.
Baseline map hash is `08fbdf0a0066429373058a919e6232517a17da91410348667155d9dd8f63df95`.
Baseline measurement UTC: `2026-10-17T09:14:03.704911+00:00`.

`docs/evidence/issue12-baseline.json` is copied from that verified **CI baseline**
record, retaining its original measurement SHA/time/run. Summary, component and
file JSON values match the local main build exactly despite the host size-tool
patch version difference. Local-to-CI comparisons are nevertheless marked
`NOT_COMPARABLE` by the conservative environment checks. CI compares two builds
in the same environment. Final stricter source/input validation and the added
empty-image test are checked again on the **final head**; the append-only
[PR #41 handoff](https://github.com/YerriSuyudHasim/noctinirs-2/pull/41) records
that SHA, all final run URLs and post-upload verification. Initial runs above
are not presented as final-head evidence.

Local `python -m unittest discover -s tests/memory -v`: **31/31 passed**, no skips
(host-tool correctness only). Tests cover actual report fixtures, missing and
malformed data, duplicate keys, increases/decreases, invalid capacities,
different SHAs/environment, missing baselines, exact deltas, warning/critical
boundaries, unknown capacities and collector build-failure propagation.

Local scaffold build: PASS. Reference integrity before work: PASS, 319 files.
Windows Git Bash's existing Class S self-test reports its chmod-000 scanner
fixture as FAIL because Windows permissions do not implement that POSIX fixture;
other cases pass. No test is changed/skipped to hide this result. Linux CI must
provide its authoritative self-test result. Production Class S conformance is
NOT APPLICABLE while layered components are absent, not a synthetic pass.
Initial collector attempt correctly failed on identical SDK legacy config
aliases; support was corrected with a regression assertion, and failed local
evidence remains in its original directory. Final-head results and URLs are
recorded in the append-only PR handoff, with corresponding retained artifacts.

Acceptance matrix:

| Criterion | Author-side status | Evidence / remaining action |
|---|---|---|
| Issue AC-1 full-SHA size reports and SDK identity | PASS | Baseline + exact-head raw artifacts and normalized provenance; CI above and final PR handoff |
| Issue AC-2 evidence-backed root cause | PASS | Address split, map, SDK memory/section scripts, component/file reports |
| Issue AC-3 budget reviewed by Group 2 Hardware Specialist | PARTIAL | Complete proposal/checklist supplied; independent verdict pending |
| Issue AC-4 no config adoption without independent review/Owner | PASS | No production configuration changes; no merge |
| Regression framework and automated failure tests | PASS | 31 host tests; CI report generated from actual builds |
| Existing Class B/reference checks | PASS | Initial CI above; exact final-head verification in PR handoff |
| ISR placement implications | PASS for scaffold scope | No app ISR; mandatory feature-specific transitive audit remains later work |
| Independent review handoff preparation | PASS | PR #41, hub contract plus Group 2 checklist on exact final head; independent verdict pending |

Review checklist for Group 2 Hardware Specialist:

- [ ] Verify exact PR head, artifact hashes, baseline SHA and actual tool versions.
- [ ] Validate fixed IRAM vs shared DIRAM interpretation, cache reservations,
  address aliasing and absence of double counting; cross-check all three reports.
- [ ] Challenge every reserve/threshold assumption, particularly DMA buffers,
  task stacks, DSP and Wi-Fi concurrency; leave an independent budget verdict.
- [ ] Confirm scaffold has no custom ISR; require future map/disassembly/data
  dependencies and cache-disabled audits before feature adoption.
- [ ] Exercise comparator failure paths, deltas, unavailable values and warnings;
  verify informational status cannot imply valid data when collection fails.
- [ ] Verify all optimization candidates are proposals; no production Kconfig,
  placement, linker, partition, task or RTOS change occurred.
- [ ] Specify later runtime tests: minimum internal free heap, largest block,
  stack high-water marks, DMA allocation success and interrupt/peripheral timing
  under Wi-Fi/flash load; Owner authorization is separate.

There is no configured automatic reviewer workflow in main; use the existing hub
handoff contract and Issue #12/PR review thread, as PR #37's checklist does.
The author cannot independently approve. Owner budget acceptance and merge
authorization remain separate decisions; no issue closure is requested.

**Host/CI analysis does not establish peak runtime heap, stack high-water marks,
memory fragmentation, or hardware timing behavior.** Static headroom does not
prove contiguous/DMA-capable heap, runtime task-stack fit or cache-disabled ISR
safety. Firmware host behavior, Class H/on-target/HIL/field tests, flashing,
electrical/optical safety and clinical claims are NOT RUN/out of scope. Dynamic
TLS and framework/task allocations are not fully represented by static DRAM.
No hardware assumptions are imported from historical `reference/**`.

PRs #38, #39 and #40, main, branch protection/settings and `reference/**` are
unchanged. Rollback is a reviewed revert of reporting/tests/docs/new workflow;
no device recovery or production memory-layout migration is involved.
