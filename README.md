# NoctiNIRS 2

NoctiNIRS 2 is a new clean-architecture ESP32-S3 firmware project starting at **v0.1.0**. It is governed by the separate private [`ai-engineering-hub`](https://github.com/YerriSuyudHasim/ai-engineering-hub) repository. Active firmware is the clean-architecture tree under `components/` and `main/`. No NoctiNIRS V2, V3, or V4 implementation has been copied into it.

## Repository layout

```text
noctinirs-2/
├── components/
│   ├── nn2_core/                 # Credential and backoff policy (no ESP-IDF)
│   ├── nn2_ports/                # nn2_port_wifi_station / cred store / prov console
│   ├── nn2_app/                  # Provision and link policy
│   ├── nn2_rtos/                 # nn2_rtos_config.h (core 0 task macros)
│   ├── nn2_ad_wifi/              # ESP-IDF STA adapter
│   ├── nn2_ad_cred/              # App NVS namespace nn2_wifi
│   └── nn2_ad_prov/              # USB Serial/JTAG one-shot console
├── main/                         # Composition root (app_main)
├── test/host/                    # Class U host tests (Unity)
├── reference/
│   ├── v2/                       # Sanitized historical snapshot
│   ├── v3/                       # Sanitized historical snapshot
│   ├── v4/                       # Sanitized historical snapshot
│   ├── MANIFEST.sha256           # Tamper-detection hashes
│   └── README.md                 # Reference policy and exclusions
├── docs/
│   ├── adr/                      # ADR-0001 .. ADR-0003
│   ├── m2-wifi-provisioning.md   # Issue #30 implementation note
│   ├── ai-engineering-hub.md
│   └── ai-engineering-hub-registration-draft.yaml
├── tools/verify-reference.ps1
├── AGENTS.md
├── project.yaml                  # Product record and reciprocal hub link
├── CMakeLists.txt
├── sdkconfig.defaults
└── VERSION
```

The root build definition, `components/`, and `main/` form the active firmware. Files under `reference/` are evidence and design input, not build inputs.

## Toolchain baseline

- Target: ESP32-S3
- Framework: ESP-IDF v6.1
- RTOS: FreeRTOS as supplied by ESP-IDF
- Firmware version: v0.1.0

ESP-IDF is not bundled with this repository. With ESP-IDF v6.1 activated:

```powershell
idf.py set-target esp32s3
idf.py build
```

The active image brings up a USB Serial/JTAG provisioning console, stores SSID and password in the application NVS namespace `nn2_wifi`, and runs a Wi-Fi station with bounded reconnect backoff. Optical acquisition, emitters, and clinical behavior are not implemented. See `docs/m2-wifi-provisioning.md`. Host unit tests:

```bash
bash test/host/run.sh
```

## Historical references

The NoctiNIRS V2, V3, and V4 trees were copied from local source folders without nested Git metadata. Generated V4 build output was excluded. Wi-Fi literals found in V2 were replaced with non-secret placeholders in the copied snapshot; the originals were not changed. See `reference/README.md` for the exact sanitization scope.

Reference files are protected by policy and a SHA-256 manifest. Run:

```powershell
pwsh -File tools/verify-reference.ps1
```

A directory name cannot enforce immutability. Any intentional reference update therefore requires explicit human authorization, a manifest refresh, and review separate from active firmware work.

## Governance and workflow

Read `AGENTS.md` before changing this repository. Normal work follows issue → branch → pull request → CI → independent review → human merge approval. Hardware, timing, power, sensor, RF, and safety claims require evidence from the applicable test class; a successful build is not hardware validation.

The relationship to `ai-engineering-hub` is documentation and governance only—there is no Git submodule. This repository's `project.yaml` links to the canonical hub record at `projects/noctinirs-2/project.yaml`; the hub record links back here. Product source remains owned by this repository.

## Current status

- M2 provisioning path (issue #30) is implemented on an issue branch: USB console, app NVS namespace `nn2_wifi`, STA reconnect. Class H is NOT RUN. No flash is authorized by that work.
- Reciprocal hub registration: prepared in both repositories; backup owner remains unassigned.
- On-target and HIL testing: not run.
