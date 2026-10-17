# Issue #10 — Phase 1–2 audit and characterization fixtures

**Scope:** static gate audit + tests only; no production gate changes, firmware changes, CI edits, branch protection changes, or hardware work. **Baseline:** `main` `4a21171353b72ceff62774ba114705e0c8be2c31`. ADR-0001 is **Proposed**, §3.3 rev1 and §3.5 are used as the provisional contract; Issue #11 remains authority for open contract questions.

## Verified current implementation

- `tools/class-s-gate.sh`: grep-only include scanner for `components/nn2_core`, `nn2_ports`, `nn2_app`; no CMake dependency audit; partial forbidden patterns; `find -type f` counts sources; no explicit symlink policy.
- `tools/class-s-gate-selftest.sh`: transient positive, no-layer, empty/missing layer, and selected include fixtures. No CMake or cross-layer fixtures.
- `.github/workflows/firmware-build.yml`: selftest runs independently; `class-s-dependency-gate` skips when no normative layer exists; `class-s-gate-invariant` distinguishes skipped/not-applicable. `layer-detect` uses a duplicated normative path list. N5 required-check configuration remains Owner-blocked.

## ADR §3.3/§3.5 traceability matrix

| ADR rule | Current gate | Phase-2 fixture | Expected |
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

**Important:** N1/N2/N4 tests are deliberately expected to fail against the current scanner. This is the Phase-2 **RED baseline**, not a green acceptance suite. Do **not** wire the tests into required CI until Phase 3 implements and verifies the fixes. After Phase 3, all tests must pass and CI run URLs must be recorded. No claims of executed local tests are made by this commit.

## Next handoff

1. Phase 3: implement complete per-layer include checks, CMake `REQUIRES`/`PRIV_REQUIRES` audit, fail-closed symlink handling, and avoid false PASS on missing/empty layers; preserve NOT APPLICABLE semantics.
2. Phase 4: add fixture execution to CI and collect green Class S + B run links at the exact PR head.
3. Phase 5: independent reviewer approval, reference-integrity green, Owner authorization before merge. N5 remains blocked on Owner branch-protection decision; no repository settings change.
