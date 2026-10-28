# Communication Foundation checkpoints

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

## Resume here

Owner scope (2026-10-22): continue the actual local `main/` and `components/`,
using PROJECT.md and historical V3/V4; no agents, remote workflow, build or flash.
The Owner will build and operate the board. Do not claim compiled/board success.

Base: `5958a0eafec6b03c2b158c4026f3f10b8a3366d1`, local `main` working tree.
Existing `.clangd`, `.vscode/`, old build and sdkconfig are user work; preserve them.
Creating a branch was denied by the environment; do not reset or rewrite history.

| Checkpoint | Deliverable | Status |
|---|---|---|
| CP1 | Source inspection, API ownership and protocol scope | SOURCE COMPLETE |
| CP2 | Board, configuration, USB provisioning and STA components | SOURCE COMPLETE |
| CP3 | Core codec, application transactions/state and UDP component | SOURCE COMPLETE |
| CP4 | Thin app_main, host tools, static checks and handoff | SOURCE COMPLETE |

Prior-turn draft: protocol/state initially shared nn2_core_protocol.c; hardware
services initially shared main/adapters/nn2_comm_esp.c. Split these drafts into the
agreed components, preserving the existing fake/diagnostic application profiles.

Verification boundary: source inspection, Python-only checks and reference hashes
are allowed. C/ESP-IDF builds, flashing, Wi-Fi RF and board tests are NOT RUN here.

Technical continuation notes will be updated at each checkpoint. Final API map:
docs/COMMUNICATION_API.md. Wire contract: docs/PROTOCOL_LOCAL.md.

CP1-CP3: reference manifest passed (319 files); GPIO18 traced in the local schematic.
New platform components are split, protocol bytes remain in Core, transaction policy
is in App. Main now wires components and polls them; old fake/diagnostic bodies moved
to main/nn2_legacy_profiles.c. No compilation or board operation performed.

CP4: Main/CLI/native test sources and all local records are complete. Python codec
checks (5), platform boundary checks (5), static dependency checks and reference
hash verification passed. C compilation and board checks remain NOT RUN. One
existing Git Bash fixture is environment-blocked; see TEST_MATRIX.md.

Next: Owner build using docs/SETUP_COMMUNICATION.md. Resume from actual first
compiler error or build output; do not reinstall an SDK or operate hardware.
Exact current implementation fingerprint and remaining limitations: docs/HANDOFF.md.

CP5 (2026-10-22): Owner's root ESP-IDF v6.1/ESP32-S3 configure stopped at the
communication stack guard: active sdkconfig had 3584 bytes. Corrected only that
setting to 12288, added the same fresh-config default, and clarified the diagnostic
and setup instructions. SDK Kconfig NOTE messages were not the stopping error.
No build or flash run by the assistant; Owner should retry the VS Code Build command.

CP6: DOCUMENTATION COMPLETE. Owner approved SoftAP onboarding, STA operation,
60-second failed-reconnect fallback and an installable Windows host. See
NETWORK_ONBOARDING.md before editing Wi-Fi/main. Implementation remains USB/STA.
CP5 build success is Owner-reported; logs/size pending, all board checks NOT RUN.
Next: CP7 policy/Wi-Fi, CP8 provisioning/main, CP9 host. Resolve unique AP password
delivery and specify the provisioning wire contract before exposing that service.

CP7-SOFTAP: Replaced draft AP+STA library with AP-only lifecycle; main starts AP
without saved credentials; UDP binds AP; client disconnect invalidates ownership.
USB diagnostics retained, provisioning disabled. Version is Owner-requested 0.1.0.
Python codec tests: 5 PASS, 2 native tests SKIPPED. Static layer audit PASS;
reference verification PASS (319). No build/flash/RF. Next: Owner build.
