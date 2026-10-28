# GitHub work compared with the local workspace

Audit date: 2026-10-27, Asia/Seoul. Evidence class: static observation only.
Repository: [YerriSuyudHasim/noctinirs-2](https://github.com/YerriSuyudHasim/noctinirs-2).

## Method and identity

Read all 32 pull-request records and all 31 issue records available at audit time.
The issues endpoint returned 63 entries including those 32 PRs; these are not 63
independent issues. Both listings fit on one 100-entry page. Read the selected
discussions on #33, #56, #30, #35, #26 and #23, file inventories for ten relevant
open PRs, and current proposal/code blobs using their exact head commits. All
selected comment listings contained fewer than 100 entries. No pagination was
silently omitted for those calls.

GitHub main, verified with `git ls-remote origin refs/heads/main`, and local HEAD
both equal `5958a0eafec6b03c2b158c4026f3f10b8a3366d1`. There is no newer main commit
missing from this checkout at audit time. The working tree contains substantial
uncommitted additions and edits; it is not identical to GitHub main.

GitHub reports 18 merged PRs, 12 open PRs (all drafts), and two closed/unmerged
PRs (#28 and #37). Merge metadata takes precedence over historical PR descriptions
that still say “open” or “not merged.” An open issue does not prove its entire
implementation is absent: #35 remains open despite the bounded #36 diagnostic
slice having merged.

Several PR heads are also available in local remote-tracking Git objects. A blob
existing in `.git` or on `origin/<branch>` does not place it in the active working
directory. No checkout, pull, merge, cherry-pick, source edit, push, or hardware
operation was performed by this audit. Only this audit document was added.

## What is already in the active checkout

| Work | GitHub evidence | Local state and relevance |
|---|---|---|
| Governance and reproducible ESP-IDF 6.1 baseline | [#5](https://github.com/YerriSuyudHasim/noctinirs-2/pull/5), [#6](https://github.com/YerriSuyudHasim/noctinirs-2/pull/6) | Merged; project record, policy and build workflows are present. Preserve provenance and reproducibility. |
| Architecture boundaries and C11/layer enforcement | [#7](https://github.com/YerriSuyudHasim/noctinirs-2/pull/7), [#38](https://github.com/YerriSuyudHasim/noctinirs-2/pull/38), [#39](https://github.com/YerriSuyudHasim/noctinirs-2/pull/39), [#43](https://github.com/YerriSuyudHasim/noctinirs-2/pull/43), [#44](https://github.com/YerriSuyudHasim/noctinirs-2/pull/44) | Merged; ADR-0001, `tools/class-s-gate.py`, C11 probes, manifests, acceptance and integration records are present. Some manifests/build wiring now have additional local edits. |
| Optical timing design | [#22](https://github.com/YerriSuyudHasim/noctinirs-2/pull/22), [#24](https://github.com/YerriSuyudHasim/noctinirs-2/pull/24) | Merged; ADR-0002 is present. It is design input for LED/ADC synchronization, not an implemented or measured scheduler. |
| RTOS/task topology and dual ADC design | [#27](https://github.com/YerriSuyudHasim/noctinirs-2/pull/27), [#29](https://github.com/YerriSuyudHasim/noctinirs-2/pull/29), [#32](https://github.com/YerriSuyudHasim/noctinirs-2/pull/32) | Merged; ADR-0003 is present. Relative ordering/design is not proof of instantiated tasks, validated priorities or timing. |
| IC datasheets | [#16](https://github.com/YerriSuyudHasim/noctinirs-2/pull/16) | Merged. Schematic assets also exist from main uploads; existence does not establish assembly/board qualification. |
| Meaningful clean-build reproducibility checks | [#40](https://github.com/YerriSuyudHasim/noctinirs-2/pull/40) | Merged; `tools/reproducible_build.py`, regression tooling and retained evidence are present. These checks need to cover the current local profile before making fresh build claims. |
| Core ring, application sequencer, fake/disabled adapters and Unity tests | [#15](https://github.com/YerriSuyudHasim/noctinirs-2/pull/15) | Merged. Core ring and sequencer production C files are unchanged against HEAD. Valuable foundation; fake demo is a separate profile, not current SoftAP execution. |
| ADS131M04 diagnostic driver | [#36](https://github.com/YerriSuyudHasim/noctinirs-2/pull/36) | Merged. `main/adapters/nn2_ads131_esp.c`, `nn2_ads131_logic.c`, `nn2_ads131_control.inc`, ports and tests are present; the three listed source files are unchanged against HEAD. Diagnostic wiring now resides in local `main/nn2_legacy_profiles.c`, selected through `CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC`. It is not active in the communication profile and is not yet synchronized optical acquisition. |
| v0.1.x roadmap | [#47](https://github.com/YerriSuyudHasim/noctinirs-2/pull/47) | Merged; `docs/ROADMAP.md` is present. Its original STA flow and historical gate text require reconciliation with newer local SoftAP decisions and proposed #62 updates. |

PR #18's earlier gate work is also included in main and subsequently hardened by
#38/#39/#43. This accounts for all 18 merged PRs, rather than counting each design
status update as a separate firmware feature.

## Work still on open branches

The counts below compare the PR change inventory with the current filesystem.
“Missing” is a path-level observation; it does not imply no equivalent local
behavior exists under another name. Files found at the same paths in the ten
inspected PR change inventories did not match the PR head contents, even after
normalizing CRLF/LF. A shared filename is not evidence of integration.

| PR / exact head | Active-directory comparison | Practical value |
|---|---|---|
| [#20 hardware documentation](https://github.com/YerriSuyudHasim/noctinirs-2/pull/20), `864047d38d01b8e7ee81d665f3a6f45046e5ba7e` | Draft `docs/hardware/nocti-board-v1/` package is not the same as the raw schematic/BOM files already on main. | Review board provenance and outstanding hardware assumptions before real drivers. Do not treat a draft board contract as approved limits. |
| [#34 STA provisioning](https://github.com/YerriSuyudHasim/noctinirs-2/pull/34), `fea6895d194c940b5641a45de5f2574e4dae2448` | 48 changed paths; 36 absent; one path deleted by that PR still exists locally. New `nn2_ad_wifi`, `nn2_ad_cred`, `nn2_ad_prov`, `nn2_rtos`, and pure reconnect helpers are not integrated. | Reconnect tests and clean adapter boundaries are useful reference. Its USB-to-NVS-to-STA behavior conflicts with current SoftAP-only scope; do not replace the working Wi-Fi implementation wholesale. |
| [#41 memory analysis](https://github.com/YerriSuyudHasim/noctinirs-2/pull/41), `b7ebdfa75b67cbd9aaa1bc7127c040537ef29e34` | All nine changed paths absent, including `tools/collect_memory.py` and `tools/memory_report.py`. | Useful when adding Wi-Fi, I2C and task/queue memory. Historical IRAM figures apply to that recorded build, not this local source. Proposed budgets remain proposals. |
| [#42 traceability](https://github.com/YerriSuyudHasim/noctinirs-2/pull/42), `23a70c7367a5cfee1f4a9565ea7710ea81905657` | All four paths absent, including `docs/TRACEABILITY.md` and PR template. | Useful for mapping requirements to code and tests before integration/release. |
| [#46 package generation](https://github.com/YerriSuyudHasim/noctinirs-2/pull/46), `f0b49bbe8392b517b2756e9dfcc15f9bce6d0249` | Three of four changed paths absent; existing firmware workflow differs. No local `tools/firmware_package.py`. | Valuable for reproducible delivery, checksum/flash-map validation and recovery documentation. Its old fake/diagnostic packages are not builds of our SoftAP/power changes. |
| [#54 bench procedure](https://github.com/YerriSuyudHasim/noctinirs-2/pull/54), `a09e0474cc29a691a9d7bad6e874caf084af4392` | `docs/bench/H-COMMS-1.md` absent. Local `BOARD_COMMUNICATION_TEST.md` is a different procedure. | Useful checklist/procedure input. Draft/parked status and unresolved hardware assumptions mean no authorization or executed evidence. |
| [#55 host protocol ADR](https://github.com/YerriSuyudHasim/noctinirs-2/pull/55), `222f9c45f82b6014db85583924da00e313e5499c` | `docs/adr/0004-host-protocol.md` absent. Local NN2-CF1 is a different contract. | High priority to reconcile before committing to host/streaming interoperability. Current file revision resolves several questions that remain mentioned in its older PR body; do not treat the body as the latest specification. |
| [#57 release/version checks](https://github.com/YerriSuyudHasim/noctinirs-2/pull/57), `f08038fdd519e1b12edd1556f51a57566f03f60e` | 18 of 19 paths absent, including version check, release notes renderer and release packet. | Useful closer to a release candidate; historical green checks do not validate this dirty working tree. |
| [#58 adapter-layer amendment](https://github.com/YerriSuyudHasim/noctinirs-2/pull/58), `d2dfaf85e14c14b01b56b53032a6ff9779413ee0` | Proposed ADR amendment is not the active ADR-0001 text. Local `platform_components.json` is a separate solution. | Reconcile adapter/platform classification and RTOS mapping before canonical integration. |
| [#60 P7 safe boot](https://github.com/YerriSuyudHasim/noctinirs-2/pull/60), `d3478d6d0747908460701e68f766247c80dbc2e0` | 13 of 24 paths absent, including bootloader hook, safe-boot application policy and emitter-safe adapter. Shared integration files differ. | High value: bootloader IO18 reset hold, zero/readback startup sequence, runtime SPIRAM check and brownout configuration. The PR deliberately fails target compilation until unspecified hardware parameters are supplied; it is not ready to paste and flash. |
| [#62 roadmap/status correction](https://github.com/YerriSuyudHasim/noctinirs-2/pull/62), `8d11a28860409eadcf463a2b91c7f68fea0e866e` | Both paths already exist but neither contains this PR revision. | Useful to distinguish deferred hardware testing from release gates and remove stale ADR status wording. Deferring a gate does not prove safe hardware behavior. |
| [#63 schema-v2 gate extension](https://github.com/YerriSuyudHasim/noctinirs-2/pull/63), `93aef984c8d40576daca3ee69b9a115179fa6dbb` | Two of six paths absent; all four shared gate/manifest files differ. | Useful fail-closed adapter/platform classification; reconcile with our local platform manifest rather than stacking two inconsistent detection systems. |

Closed/unmerged #28 concerns cloud toolchain notes. Closed/unmerged #37 was a
memory-observability draft overlapping #41; it is not an additional implemented
memory subsystem.

## Local additions versus GitHub implementation

Current local source adds SoftAP-only `nn2_wifi`, UDP command `nn2_net`,
`nn2_config`, `nn2_provision`, `nn2_core_protocol`, `nn2_app_comm`, host CLI and
communication tests. This is local implementation of capabilities related to
issues #48/#50/#51; it does not establish those issues' ADR-0004 acceptance.

Board power initialization, MAX17048/BQ24295 access, automatic charge-enable
request and 60/300-second battery monitor are also local additions. They are not
part of the current GitHub main baseline. They have not acquired the historical
CI/review evidence of merged source simply by sharing the same repository.

## Material integration gaps

### Wire protocol

| Property | Local NN2-CF1 | Proposed ADR-0004 at #55 head |
|---|---|---|
| Magic | `N2` (2 bytes) | `NNS2` (4 bytes) |
| Header | 12 bytes | 28 bytes |
| Integrity | CRC-16/CCITT-FALSE, 2 bytes | CRC-32/ISO-HDLC, 4 bytes |
| Payload cap | 128 bytes | 1440 bytes |
| Request ID | Header | Payload |
| Stream/session envelope | Current control slice; no real FRAME producer | Session ID, message sequence, timestamp, FRAME/SESSION_REPORT/EVENT types |

Both use explicit little-endian encoding, but their datagrams are incompatible.
Do not claim the local CLI conforms to ADR-0004. Resolve and version this choice
before integrating the PC host or adding streaming. This is a recommendation,
not acceptance of an unmerged proposal or an instruction to replace current code.

### Boot safety and shared I2C

Local reset hold begins in app_main. PR #60 adds a bootloader hook and a separate
LED-safe initialization sequence. These are materially different safety coverage.
Neither static inspection nor a bootloader hook proves the preceding ROM-to-hook
interval safe. Actual electrical/reset behavior still requires approved evidence.

If PR #60 is adopted, its boot-time LED I2C ownership and reset-release sequence
must be serialized with `nn2_board_power` on the shared SDA/SCL wires. It cannot
be inserted alongside a second owner without a reviewed boot order and cleanup
contract. The local power library's exclusive-phase design is not a complete
real-time LED scheduler.

### Implemented source versus active runtime

ADS131 diagnostic code is retained but selected only by its diagnostic profile.
SoftAP communication currently simulates acquisition. Accepted optical/RTOS ADRs
do not mean real PCA9956B drive, SlotPlan, synchronized ADC frames, ADS1299 or
dual-rate capture already execute. Issue #31 and roadmap items still represent
future work.

## Attribution

GitHub author accounts alone do not identify the model; many agents act through
the Owner's account and some commits identify only Cursor Agent.

- #15 and #36 branch commits explicitly use author `Codex Firmware Developer`.
  #39 and #43 PR records explicitly identify Codex as integration author.
- #62 explicitly declares model `grok-4.7-high-fast` and its Agent 08 role.
  Other records declare Architect, RTOS, Hardware, Verification or Reviewer roles,
  but those role names alone are not proof of a particular model.
- Designs, reviews, tests, tooling and documentation are useful engineering
  outputs even where they did not add an executing firmware feature.

## Recommended order

1. Keep the already merged ring/sequencer, ADS131 slice, tests and architecture
   protections. Avoid rewriting these solely because the default profile changed.
2. Reconcile the host wire contract (#55/#56 versus NN2-CF1) and safe-boot scope
   (#60) before real LED acquisition or host streaming work.
3. Reconcile architecture classification (#58/#63 versus the local platform
   manifest) and restore meaningful full-profile CI for the current source.
4. Review the memory tooling (#41), then packaging/version/traceability tooling
   (#46/#57/#42). Rebuild evidence for the actual integrated commit.
5. Retain SoftAP-only behavior per the current Owner direction. Use #34 as a
   reference for tests/boundaries; postpone its STA-specific functionality unless
   the product direction changes. Bench drafts and hardware documents stay inputs
   to a separately approved procedure, not automatic permission to operate hardware.

No incoming PR was integrated and no test/build outcome was newly claimed here.
Reference integrity checks passed before and after the source inspection: 319
files. This audit does not replace independent code review, CI or hardware tests.
