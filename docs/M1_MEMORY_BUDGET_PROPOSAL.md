# M1 memory observability and budget — PROPOSAL (Issue #12)

Status: **Proposed, not approved**. Evidence: Class B only. No runtime (Class U) or hardware (Class H) validation claimed. ESP-IDF v6.1, commit `fff9895c82d744c7237be8847347bdd1b07c6643`; target ESP32-S3.

## Measurements (do not mix baselines)

| CI evidence | Fixed IRAM | DIRAM used | DIRAM available | Source |
| --- | ---: | ---: | ---: | --- |
| Historical build job `116801754210` (SHA `cd4ed7f889…`) | 16,384 / 16,384 B | 48,242 B | 293,518 B | [job](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/38847664721/job/116801754210) |
| Wi-Fi build job `117279863535` (SHA `8a9c33aeee…`) | 16,384 / 16,384 B | 107,750 B | 234,010 B | [job](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/38993201955/job/117279863535) |

Both builds succeeded. The 59,508 B DIRAM difference is **not** attributable to any single component without matched-config and size-components/size-files analysis. The 16 KiB fixed IRAM section being full does **not** itself indicate a linker failure: IRAM-placed text may reside in DIRAM. Report the complete memory-type breakdown and internal heap headroom rather than treating fixed IRAM 100% as a standalone failure.

## Proposed gates (subject to RTOS and Hardware Specialist review)

- **IRAM:** track fixed IRAM, DIRAM `.text`, and per-object IRAM-placed code; no automatic failure based solely on fixed IRAM 100%. Confirm interrupt/callback placement from `size-files`, map and disassembly where needed.
- **DIRAM:** preliminary *static* remaining target >= **128 KiB** in `idf.py size` for the v0.1.x Class B baseline. This is a planning threshold, **not** a proven runtime-safe heap reserve. Reassess after Wi-Fi, ADC buffers, stacks and task allocations; record minimum free internal heap and largest free internal block in later Class H testing.
- **DRAM:** include `.data`, `.bss`, static stack allocations and DMA-capable buffers in a component-level budget; derive numeric caps from matched SHA reports before approval. Avoid double-counting shared DIRAM/DRAM pools.
- **Flash:** record app binary size, flash `.text`/`.rodata`, partition size and remaining partition headroom. Proposed app partition occupancy <= **80%** until the partition table and OTA strategy are locked; not an absolute flash-size claim.
- **Regression:** compare identical ESP-IDF commit, target, sdkconfig and build flags. Publish absolute and delta bytes by memory region, component and file. Treat a changed configuration as a separate experiment.

## Required evidence to resolve Issue #12

1. Download the `class-b-build-<SHA>` artifact for `main` SHA `4a21171353b72ceff62774ba114705e0c8be2c31` and the Wi-Fi PR SHA under the same IDF commit. The CI job now records `size.txt`, `size-components.txt`, `size-files.txt`, `linker-map.sha256`, generated `sdkconfig`, `toolchain.txt`, and `build/noctinirs2.map`.
2. Build matched baselines or explain configuration differences; compare the complete generated `sdkconfig` and map sections.
3. Audit all actual `IRAM_ATTR`, DRDY ISR, SPI callbacks, and their transitive calls for cache-disabled safety. `size-files` alone does not prove an ISR is IRAM-safe.
4. Review the thresholds with the RTOS Specialist and Hardware Specialist. Document the accepted limits and measurement method.
5. Do **not** change Kconfig, move code out of IRAM, or close the issue without independent review and Owner authorization.

No modification to `reference/**`, no firmware behavior changes, no merge or hardware operations are part of this proposal.
