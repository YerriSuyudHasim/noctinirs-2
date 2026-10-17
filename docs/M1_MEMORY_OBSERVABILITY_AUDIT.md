# Issue #12 — verified Class B memory observability audit

Status: **Evidence captured; budget remains proposed, not independently approved.** PR #37 run [38998172511](https://github.com/YerriSuyudHasim/noctinirs-2/actions/runs/38998172511) passed, build job `117295232646`. Exact source SHA `29449733a6f011a43be563448a88b83e6fac1c27`, IDF v6.1 commit `fff9895c82d744c7237be8847347bdd1b07c6643`.

## Artifact inventory and integrity

Artifact `12088267134` includes `evidence/size.txt`, `size-components.txt`, `size-files.txt`, `sdkconfig`, `toolchain.txt`, `linker-map.sha256`, and `build/noctinirs2.map` (plus ELF/BIN). Map SHA-256 from artifact: `3fe7c891aad4a0418e01716b93836ea7d7efacd9013f5f5bd0efa5a437ead837`. Generated sdkconfig SHA-256: `5c6b7e1073053eb0580faae611d4736d6d72424934035b865166f9659d0c626f`.

## Static memory report (exact artifact)

| Region | Used B | Remaining B | Notes |
| --- | ---: | ---: | --- |
| Fixed IRAM | 16,384 | 0 | 15,356 `.text` + 1,028 `.vectors`; **not proof of overflow** |
| DIRAM | 48,242 | 293,518 | `.text` 32,935; `.data` 12,699; `.bss` 2,608 |
| Flash code | 69,114 | — | `.text` |
| Flash data | 38,760 | — | `.rodata` 38,488 + metadata |
| App partition | 170,048 | 878,528 | `0x29840` binary / `0x100000` partition; ~83.8% free |

Largest DIRAM contributors by archive: `libspi_flash.a` 14,403 B; `libfreertos.a` 9,175 B; `libheap.a` 5,490 B; `libesp_hw_support.a` 5,438 B; `libesp_hal_mspi.a` 2,810 B. The application archive `libmain.a` contributes only 66 B of flash code and **zero IRAM/DIRAM** in this scaffold build. Do not interpret static DIRAM remaining as runtime free heap.

Generated sdkconfig confirms `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y`, `CONFIG_SPI_MASTER_ISR_IN_IRAM=y`, `CONFIG_ESP_WIFI_IRAM_OPT=y`, and `CONFIG_SPIRAM` disabled. Enabled framework options are not proof that a custom ISR or Wi-Fi stack is linked and active.

## ISR source scope

At this SHA, `main/app_main.c` contains only two log statements. No application ISR or acquisition task is implemented. Therefore custom DRDY/SPI ISR placement and transitive cache-disabled safety are **not testable on this scaffold**. Must audit implementation PR(s) separately before merging them.

## Proposed budget decision (pending independent signoff)

1. Keep **static DIRAM remaining >= 128 KiB** as an early-warning planning floor, not a validated runtime guarantee. For this scaffold, 293,518 B remaining gives 162,446 B headroom above that floor.
2. Keep **app partition utilization <= 80%** as a provisional planning threshold (currently ~16.2%). Revisit when OTA partition scheme is chosen.
3. Do not fail on fixed IRAM 100% alone. Fail on linker overflow or a demonstrated unsafe ISR chain; track IRAM-placed `.text` and DIRAM code growth per SHA.
4. Record minimum free **internal** heap, largest free internal block, DMA allocations and stack high-water marks in later hardware/runtime validation. Static linker reports cannot substitute for these.
5. Require matching `sdkconfig` hash, toolchain, and source-feature scope for byte-by-byte regressions. Historical Wi-Fi DIRAM usage of 107,750 B is a different SHA/configuration; its 59,508 B difference from scaffold is **not** a controlled Wi-Fi attribution.

## Remaining signoff / acceptance

- [x] CI build passed and all three size outputs archived.
- [x] Linker map and generated sdkconfig retrieved and inspected.
- [x] Scaffold source inspected: no application ISR to validate.
- [ ] Matched-feature Wi-Fi / ADS implementation comparison and ISR transitive call graph, when those features are integrated.
- [ ] Independent RTOS Specialist and Hardware Specialist review of proposed thresholds.
- [ ] Owner approval for budget adoption, PR merge and Issue #12 closure.

**No IRAM optimization, Kconfig changes, reference edits, or hardware validation.**
