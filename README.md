> **V4 restart branch: `feat/35-v4-idf61-restart`.** We are starting over from
> the V4 runtime, adapted to ESP-IDF 6.1 with explicit NN2 modifications.
> Build the [V4 migration project](firmware/v4-idf61/README.md); the repository-root
> firmware and the sections below retain the previous feat35 implementation.
> This is a source checkpoint; build/link and board qualification remain pending.

> Current local communication revision (2026-10-27): ADR-0004/1 replaces CF1.
> See [local ADR binding](docs/adr/0004-local-implementation.md) for exact wire
> layouts, CLI commands and simulation limits, and [evidence](docs/ADR004_EVIDENCE.md).
> SoftAP stays active. The current opt-in [LED-only bring-up](docs/LED_BRINGUP.md)
> adds real sequential LED drive and an early reset hook; ADC/frames remain absent.

# NoctiNIRS 2

NoctiNIRS 2 targets ESP32-S3 / ESP-IDF v6.1. Current v0.1.0 is **SoftAP-only**:
SSID **NoctiNIRS**, public prototype password **123456789**, device **192.168.4.1**,
UDP **3333**. WPA2/WPA3 transition mode; one computer; no router required.
By default, LED/ADC commands remain simulated and emitter reset stays asserted.
CONFIG_NN2_LED_BRINGUP enables real LED drive on explicit START: 20 optical slots,
default 5 Hz / 30 mA, maximum 10 Hz. SIMULATED does not mean LEDs are disabled
in this opt-in mode. Read the bring-up procedure before changing profiles.

Start with [current SoftAP setup](docs/SOFTAP_CURRENT.md). The Owner has reported
working SoftAP control tests on the preceding communication candidate; the new
LED candidate has host/static evidence only. The Owner performs full builds and
flash. Previous STA provisioning instructions
are superseded; no router credentials or USB provisioning are needed.


## Repository layout

```text
noctinirs-2/
├── main/                         # Active v0.1.0 firmware
├── reference/
│   ├── v2/                       # Sanitized historical snapshot
│   ├── v3/                       # Sanitized historical snapshot
│   ├── v4/                       # Sanitized historical snapshot
│   ├── MANIFEST.sha256           # Tamper-detection hashes
│   └── README.md                 # Reference policy and exclusions
├── docs/
│   ├── ai-engineering-hub.md
│   └── ai-engineering-hub-registration-draft.yaml
├── tools/verify-reference.ps1
├── AGENTS.md
├── project.yaml                  # Product record and reciprocal hub link
├── CMakeLists.txt
├── sdkconfig.defaults
└── VERSION
```

The root build definition, `main/` and `components/` form the active firmware. Platform components are `nn2_board`, `nn2_config`, `nn2_provision`, `nn2_wifi`, `nn2_net`; the existing `nn2_core`, `nn2_ports`, and `nn2_app` are extended. Files under `reference/` are evidence and design input, not build inputs.

## Toolchain baseline

- Target: ESP32-S3
- Framework: ESP-IDF v6.1
- RTOS: FreeRTOS as supplied by ESP-IDF
- Firmware version: v0.1.0

ESP-IDF is not bundled with this repository. With ESP-IDF v6.1 activated:

```powershell
idf.py -B build/communication -D SDKCONFIG=build/communication/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication' -D IDF_TARGET=esp32s3 build
```

Run this yourself in an activated ESP-IDF v6.1 terminal; the separate build/configuration preserves the existing root sdkconfig and old build cache. See the setup guide for details. Changing a defaults file does not override an existing generated configuration.

## Historical references

The NoctiNIRS V2, V3, and V4 trees were copied from local source folders without nested Git metadata. Generated V4 build output was excluded. Wi-Fi literals found in V2 were replaced with non-secret placeholders in the copied snapshot; the originals were not changed. See `reference/README.md` for the exact sanitization scope.

Reference files are protected by policy and a SHA-256 manifest. Run:

```powershell
pwsh -File tools/verify-reference.ps1
```

A directory name cannot enforce immutability. Any intentional reference update therefore requires explicit human authorization, a manifest refresh, and review separate from active firmware work.

## Governance and workflow

For this local development track, the Owner has explicitly selected the bounded
workflow in `docs/DECISIONS.md`: local implementation now, with build/flash performed
by the Owner. Remote publication and independent review are not part of this task.
The following describes the repository's normal remote workflow.

Read `AGENTS.md` before changing this repository. Normal work follows issue → branch → pull request → CI → independent review → human merge approval. Hardware, timing, power, sensor, RF, and safety claims require evidence from the applicable test class; a successful build is not hardware validation.

The relationship to `ai-engineering-hub` is documentation and governance only—there is no Git submodule. This repository's `project.yaml` links to the canonical hub record at `projects/noctinirs-2/project.yaml`; the hub record links back here. Product source remains owned by this repository.

## Current status

- Local working tree prepared.
- Private GitHub repository URL: verified from local `origin`.
- Reciprocal hub registration: prepared in both repositories; human owner, backup owner, risk class, and evidence-retention fields remain unresolved.
- ESP-IDF build: NOT RUN for this candidate, per Owner instruction. Local v6.1 headers were inspected.
- On-target and HIL testing: not run.

## Issue 35 diagnostic candidate (draft PR 36)

The fake ADC / disabled emitter demo remains selectable. The default for a fresh
configuration is now Communication Foundation. The existing opt-in
ADS131 diagnostic is built with `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ads131-diagnostic"`
and a clean generated sdkconfig. It initializes, verifies and reads four signed
channels, stops, then demonstrates explicit reinitialization. It has no emitter
control or USB LED-on route. Compilation is not authorization to install/run it.
See [candidate contract](docs/ISSUE35_DIAGNOSTIC_CONTRACT.md) and
[test/recovery guide](docs/ISSUE35_DIAGNOSTIC_TEST_GUIDE.md). All physical tests
remain NOT RUN; #19 limitations are unchanged.
