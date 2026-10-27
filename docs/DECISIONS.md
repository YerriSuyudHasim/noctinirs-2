# 2026-10-27: ADR-0004 adoption and LiPo setpoints

The Owner requested migration from local CF1 to ADR-0004, a comparison with V3/V4,
and communication improvements. Single-cell LiPo fast-charge nominal 512 mA is
accepted by the Owner; the cell maximum is 4.20 V. The implementation selects
4176 mV for tolerance headroom and reads back current/voltage before CE enable.
See [the local ADR binding](adr/0004-local-implementation.md) and
[verification](ADR004_EVIDENCE.md). SoftAP is retained. The upstream proposal is
preserved with source SHA attribution, not marked independently Accepted. Real
FRAME production, full optical state completion and bootloader safe boot remain
unimplemented; no flash/hardware action is authorized or performed by this edit.

# Local implementation decisions

## 2026-10-27: Board power and battery telemetry

Owner requested automatic charge enable after boot checks, battery telemetry
every 60 seconds idle / 300 seconds acquiring, and isolation from future LED I2C.
See [BOARD_POWER.md](BOARD_POWER.md) and the
[local ADR](adr/2026-10-27-board-power-and-shared-i2c.md). This supersedes the older
blanket prohibition on initializing the physically shared bus only for exclusive
power-management phases. RESET_LED stays asserted. Earlier communication-only
evidence does not cover this change; see BOARD_POWER_EVIDENCE.md. Target build,
board testing, battery compatibility and specialist reviews remain pending.

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

## 2026-10-22 â€” Owner-directed implementation track

The Owner explicitly authorized reversible local firmware development without
issues, independent reviewers, other agents, PRs or CI as prerequisites. All edits
are in the selected local GitHub folder. Governance files are not rewritten.
The latest Owner instruction reserves compilation and flashing for the Owner.

## Communication Foundation scope

Extend existing nn2_core/nn2_app/nn2_ports. Add platform components nn2_board,
nn2_config, nn2_provision, nn2_wifi and nn2_net. Keep app_main as startup wiring and
the single polling owner. Do not create one task per component. Existing fake and
ADS131 diagnostic profiles remain available as separate, explicit selections.

The legacy hardcoded default AP password is not adopted. No credentials are put in
active source. No reference snapshot is edited. The earlier stop was superseded
by the Owner's explicit direction to continue with local firmware implementation.

ADR-0004 is not present in this checkout (ROADMAP.md calls it Proposed, not on main).
Use the isolated local NN2-CF1 contract in PROTOCOL_LOCAL.md for the new CLI/firmware.
It is not V3/V4 compatibility and does not accept or replace an unseen remote ADR.

Use the 2026-10-16 local schematic to trace GPIO18 to RESET_LED/PCA9956B RESET#.
Hold it low throughout the communication profile; never initialize the LED bus.
Physical board revision, wiring, power-on/reset transients and off effectiveness
still require board validation. A software GPIO success is not physical proof.

Do not modify the existing local sdkconfig or old build cache. Provide a separate
communication defaults file and isolated build instructions. Keep PSRAM disabled
for the existing board contract. No SDK installation, upgrade or remote publishing.

## Explicit platform component classification

The previous detector allowed only the three inner nn2_* component directories.
The agreed five platform libraries are explicitly listed in
tools/platform_components.json. The detector still rejects unknown directories,
duplicates, path traversal, symlinks and attempts to reclassify inner layers.
tools/layers.json forbids these platform headers in inner code; existing CMake
dependency restrictions are preserved. Host header probes remain scoped to the
three inner layers. This is a technical component-boundary extension, not disabling
a check. Tests include a negative core -> nn2_board include.

## Owner-approved SoftAP onboarding and desktop host

Normal setup will use password-protected SoftAP, followed by STA operation. Keep
AP available during candidate validation and until the host confirms STA handover.
Persist only a successful candidate; preserve accepted credentials on failed trials.
Open recovery AP after 60 seconds of unavailable STA; host heartbeat loss alone
does not trigger AP. Keep emitters off and require a fresh session after recovery.
USB is optional service access. See NETWORK_ONBOARDING.md for component ownership,
contract work, password-delivery input and CP6-CP9. This supersedes USB-only product
onboarding, but current source remains USB/STA until implementation is complete.
The host target is an installable Windows desktop application reusing the Python
codec; UI framework and packaging tools have not yet been selected.

Owner reports the CP5 source builds successfully. No hardware test was performed;
the device is at home while development is on the lab computer. Build logs and
size are pending, so this is reported build evidence, not independent verification.

## Superseding Owner decision: SoftAP-only v0.1.0

Owner explicitly requested SSID NoctiNIRS and password 123456789. These are public
prototype configuration, not discovered private credentials. This explicitly
supersedes earlier no-fixed-password planning. Future board 2 supplies the router.
No STA onboarding, credential extraction or QR is part of this release. Source
version is 0.1.0 as requested. Shared SSIDs are not unique device identities.
# 2026-10-27 LED-only bring-up update

Owner requested all optical channels, default 30 mA, default Fs=5 Hz, maximum
10 Hz. The exact accepted Fs set remains {1,2,4,5,8,10}. ADR-0005 records the
optional real LED binding, one-hot 20-slot scan, pending reviews, exclusive I2C
ownership and deferred battery reads. Default communication stays simulated;
LED-profile START requires explicit operator intent and an approved bench procedure.

