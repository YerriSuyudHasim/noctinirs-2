# Communication Foundation local handoff

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

## Current resume point

SoftAP-only source revision completed; Owner should build this revision. Five
Python codec checks and static layer audit passed; two native checks skipped.
No new build or board run. Source base unchanged, working tree uncommitted.

## Superseded resume point

Source checkpoints CP1-CP5 are complete. The Owner reports build success after CP5;
logs, warnings and size are not independently archived. CP6 records the approved
SoftAP/STA redesign in NETWORK_ONBOARDING.md. **Next action: CP7 onboarding policy
and Wi-Fi adapter, followed by CP8 provisioning/main integration.** Do not flash or
run hardware automatically. Do not resume remote workflow or contact other agents.

## Exact source identity

- Local root: `C:/Users/doeki/Documents/GitHub/noctinirs-2`.
- Branch: `main`, uncommitted working tree; no push/commit/tag.
- Base commit: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1`.
- Implementation SHA-256: `c5675e829b8c3cb03f006d3a8ddffe14f98193e4abedf5594ef50626e5bbf273`.
- Per-file input inventory: `docs/evidence/communication-source.json`. Regenerate
  after source edits with `python tools/communication_source_identity.py`.
- Fingerprint covers active code/configuration/tools/tests plus reference manifest,
  not documentation, editor state or generated SDK/build files.

The environment denied creation of a local branch; existing work was not reset.
`.clangd`, `.vscode/` and previous build output are preserved. CP5 changed only
CONFIG_ESP_MAIN_TASK_STACK_SIZE in root sdkconfig, from 3584 to 12288 bytes.

## Implemented source

- Main starts board off protection, NVS, USB, persistent boot identity, STA,
  communication application and UDP, then polls services from one owner task.
- Added platform libraries: nn2_board, nn2_config, nn2_provision, nn2_wifi, nn2_net.
- Extended nn2_core with explicit codec/CRC; nn2_app with command/state/cache/
  lease policy and provisioning validation; nn2_ports with storage/safety contracts.
- GPIO18 RESET_LED stays low. No LED-on function or ADC capture in communication.
- Wi-Fi STA reconnect/backoff, IP loss/reassociation, link-epoch invalidation,
  nonblocking bounded UDP batches, socket recovery and visible counters.
- 14 NN2-CF1 commands, duplicate replay, request-ID conflict/order checks, 3-second
  heartbeat lease, persistent fault state and SIMULATED hardware outcomes.
- Independent Python CLI, golden vectors, native C loopback and deterministic C
  policy/provisioning tests. Native tests are supplied as source, not claimed run.
- Existing fake/ADC diagnostic bodies moved to main/nn2_legacy_profiles.c and
  remain separately selectable. The original monolithic draft adapter was removed.

API map: COMMUNICATION_API.md. Wire details: PROTOCOL_LOCAL.md.
Setup: SETUP_COMMUNICATION.md. Future scope: ROADMAP_LOCAL.md.

## Verification actually performed

- Owner-reported root configure reached the communication stack guard and failed
  with a 3584-byte main stack. CP5 corrects the active setting and fresh defaults;
  no post-fix build has been run. SDK Kconfig NOTE messages were nonfatal in that log.

- Five Python codec/provisioning/synthetic-sequence tests PASS; two native C
  integration tests explicitly skipped because no executable was built.
- Five platform boundary tests PASS, including rejection of core -> board include.
- Static include/CMake dependency audit PASS; git diff --check PASS.
- Reference integrity PASS before and after work: all 319 files unchanged.
- Existing manifest suite: 14 PASS, one environment-blocked Git Bash integration
  failure (MSYS named-object access denied). No bypass or disabled assertion.
- C host compilation/execution, IDF build, warnings, RAM/flash/binary size,
  on-target/RF/soak/restart tests: NOT RUN per Owner instruction.

The SDK header inventory identifies v6.1.0; exact SDK Git revision remains unverified
because that Git query was denied. No SDK installation or substitution occurred.
TEST_MATRIX.md separates static, Python, native and board evidence.

## Remaining risks / inputs

This code is not yet compiler-verified. The first build may expose integration or
configuration errors. Hardware assembly must match the supplied schematic; startup
reset transients, electrical off effectiveness, USB persistence, memory watermarks
and Wi-Fi recovery have no measured evidence. Board revision is not invented.

NN2-CF1 is a new local contract, not legacy V3/V4 compatibility or an approval of
missing Proposed ADR-0004. CRC/endpoint ownership are not authentication; use a
controlled WPA2 LAN. NVS credentials are not encrypted by this development profile.
NVS erase/restore invalidates boot-counter identity history. USB replacements apply
on reboot if Wi-Fi has already started. Synchronous NVS/logging may delay polls;
no hard real-time guarantee is claimed. The selected heap/latency/stack limits are
predeclared test targets, not measured results.

Board procedure: BOARD_COMMUNICATION_TEST.md. If reset/off or protocol behavior
fails, stop the affected board test, retain source/config/logs and correct locally.
Rollback must preserve user edits and restore only this track's files from a
reviewed copy/diff; never blanket-reset the workspace or alter reference snapshots.
