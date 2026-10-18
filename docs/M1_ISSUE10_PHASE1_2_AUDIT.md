# Issue #10 — Phase 1–2 audit and characterization fixtures

**Historical Phase 1–2 scope:** static gate audit + characterization tests. The baseline descriptions and RED results below describe that phase, not the latest scanner. Phase 3–4 status is appended below and in [the parser audit](M1_ISSUE10_PARSER_AUDIT.md). **Baseline:** `main` `4a21171353b72ceff62774ba114705e0c8be2c31`. ADR-0001 is **Proposed**; §3.3 rev1 and §3.5 are the provisional contract. Issue #11 retains authority for open contract questions.

## Phase 1 baseline implementation

- `tools/class-s-gate.sh`: grep-only include scanner for `components/nn2_core`, `nn2_ports`, `nn2_app`; no CMake dependency audit; partial forbidden patterns; `find -type f` counts sources; no explicit symlink policy.
- `tools/class-s-gate-selftest.sh`: transient positive, no-layer, empty/missing layer, and selected include fixtures. No CMake or cross-layer fixtures.
- `.github/workflows/firmware-build.yml`: selftest runs independently; `class-s-dependency-gate` skips when no normative layer exists; `class-s-gate-invariant` distinguishes skipped/not-applicable. `layer-detect` uses a duplicated normative path list. N5 required-check configuration remains Owner-blocked.

## ADR §3.3/§3.5 traceability matrix

| ADR rule | Phase 1 gate | Phase-2 fixture | Expected |
|---|---|---|---|
| Platform `esp_*`, `freertos/*`, `driver/*`, `lwip/*`, `hal/*`, `soc/*`, `rom/*`, `esp_private/*`, `sdkconfig.h` | Partial regex; `esp_*` broadly caught, `esp_private/*` caught by `esp_*` | Existing `tools/class-s-gate-selftest.sh` | FAIL for forbidden include |
| BLE `nimble/*`, `host/ble_*`, `bt_*` | `nimble/*` only; others absent | `test_n1_ble_header` | FAIL |
| NVS `nvs.h`, `nvs_flash.h`, `nvs_*.h` | Partial `nvs*.h` | Existing selftest | FAIL |
| Unprefixed `FreeRTOS.h`, `task.h`, `queue.h`, `semphr.h`, `timers.h`, `event_groups.h` | Only `FreeRTOS.h` | `test_n1_unprefixed_freertos` | FAIL |
| Adapter `nn2_adapter_*`, `nn2_adapters_*` | Singular only | `test_n1_adapter_plural` | FAIL |
| Core forbids `nn2_app_*`, `nn2_port_*`; Ports forbids `nn2_app_*`; Application forbids adapters | Missing layer-specific checks | `test_n1_cross_layer_core_to_ports`, `test_n1_cross_layer_ports_to_app` | FAIL |
| Core `REQUIRES/PRIV_REQUIRES`: none | Not implemented | `test_n2_core_requires_idf` | FAIL |
| Ports CMake: `nn2_core` only | Not implemented | `test_n2_ports_priv_requires_illegal` | FAIL |
| App CMake: `nn2_core`, `nn2_ports` only | Not implemented | `test_n2_app_multiline_illegal` | FAIL |
| Existing layer with no `.c/.h/.cpp/.hpp` | Already fails | `test_empty_layer_fails` | FAIL |
| Missing normative layer when some exist | Already fails | `test_missing_layer_fails` | FAIL |
| No normative layers | NOT APPLICABLE, not evidence | `test_no_layers_not_applicable` | exit 3 |
| Symlink bypass N4 | Not explicitly handled | `test_n4_symlink_file_fails_closed` | FAIL (proposed policy, needs review) |
| Valid layered sources + legal CMake | Existing grep passes | `test_valid` | PASS |

**Known scanner limits:** ADR defers macro-expanded includes (G3), transitive includes (G4), generated headers (G5), and `<stdatomic.h>` remains manual-review-only. Do not claim these are automatically covered. The normative-path duplication and unknown `nn2_*` directories require an explicit source-of-truth decision with Issue #11; do not silently invent new paths.

## Running the characterization tests

```sh
python3 -m unittest discover -s tests/gate-fixtures -p 'test_*.py' -v
```

**Historical result:** N1/N2/N4 fixtures were deliberately RED against the Phase 1 scanner. Phase 3 subsequently implemented the gate; Phase 4 connected the regression suite to CI. The original record does not claim tests were executed in Phase 1–2.

## Historical next handoff

1. Phase 3: implement complete per-layer include checks, CMake `REQUIRES`/`PRIV_REQUIRES` audit, fail-closed symlink handling, and avoid false PASS on missing/empty layers; preserve NOT APPLICABLE semantics.
2. Phase 4: add fixture execution to CI and collect green Class S + B run links at the exact PR head.
3. Phase 5: independent reviewer approval, reference-integrity green, Owner authorization before merge. N5 remains blocked on Owner branch-protection decision; no repository settings change.

## Phase 3–4 continuation traceability (2026-10-17)

Accountable activity: engineering implementation and Verification for Issue #10, not independent approval. Specialist required: independent Reviewer/Architecture review of the supported parser subset. No RTOS/hardware behavior or architectural contract is changed.

| Provisional ADR requirement | Implementation and regression evidence |
|---|---|
| §3.3 platform, BLE, NVS, unprefixed FreeRTOS, singular/plural adapter patterns | `PLATFORM`; `test_n1_complete_adr_forbidden_matrix` exercises all 23 representative headers in each of the three layers (69 subcases). Legacy Bash fixtures retain ESP32-S3 ROM/xtensa cases. |
| §3.3 Core→App/Ports, Ports→App, all inner layers→Adapters forbidden | `CROSS` and path-segment checks; `test_n1_cross_layer_matrix`, relative-path and adapter fixtures. |
| §3.5 Core dependencies: none | `LAYERS['nn2_core']`; `test_n2_core_requires_idf`, quoted keyword/dependency fixtures. |
| §3.5 Ports dependencies: Core only | `LAYERS['nn2_ports']`; `test_n2_ports_priv_requires_illegal`, legal quoted/private fixture. |
| §3.5 App dependencies: Core/Ports only | `LAYERS['nn2_app']`; multiline illegal and legal public/private fixtures. |
| §4 every normative CMake script must be audited; parse errors fail | Whole-script restricted parser; extra/nested registrations, nested CMakeLists, dynamic and malformed syntax fail closed; nested scripts are unsupported rather than silently skipped. |
| §3.5 unknown `nn2_*` paths fail | Scanner and actual workflow detection tests cover directories, files, and broken links. Path list remains duplicated pending N6 / Issue #11. |
| §4 zero files or incomplete hierarchy cannot pass | Empty/missing fixtures; valid header-only Ports remains supported. |
| §4 absent hierarchy = NOT APPLICABLE, not evidence | Exit 3 and workflow skip fixtures; malformed/symlinked paths cannot select this branch. |
| §4 tool errors fail | Explicit directory enumeration, invalid UTF-8 and injected PermissionError fixtures; Bash permission test runs on non-root POSIX CI. |
| N4 symlink handling | Fail closed for components, layer, nested directory, file, and broken links; Windows junctions also rejected. |
| §3.3 standard headers / `<stdatomic.h>` | This gate enforces the forbidden patterns, not a full standard-header whitelist. `<stdatomic.h>` remains manual RTOS review; no automated claim. |
| §3.3 G3/G4/G5 | Macro-expanded, transitive, and generated includes remain deferred; no automated absence claim. |

The [parser audit](M1_ISSUE10_PARSER_AUDIT.md) records the new findings, fixtures, supported syntax, local results and the verified incoming SHA's CI. The append-only PR #38 continuation handoff records the implementation SHA and its own CI URLs after push. Phase 5 still requires independent approval and Owner action; N5 remains blocked.
