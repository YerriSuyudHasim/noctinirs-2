# NoctiNIRS 2

NoctiNIRS 2 is a new clean-architecture ESP32-S3 firmware project starting at **v0.1.0**. It is governed by the separate private [`ai-engineering-hub`](https://github.com/YerriSuyudHasim/ai-engineering-hub) repository. The active firmware is intentionally a minimal scaffold; no NoctiNIRS V2, V3, or V4 implementation has been copied into it.

## Repository layout

```text
noctinirs-2/
├── main/                         # Composition root
├── components/                   # Active v0.1.0 layers (issue #35)
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

The root build definition, `main/`, and `components/` form the active firmware. Files under `reference/` are evidence and design input, not build inputs.

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

`app_main` holds the ADS131M04 SYNC/RESET line low and creates the `nn2_ads131` task. It does not call `start`, and it does not claim on-target sampling.

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

- Local working tree prepared.
- Private GitHub repository URL: verified from local `origin`.
- Reciprocal hub registration: prepared in both repositories; human owner, backup owner, risk class, and evidence-retention fields remain unresolved.
- ESP-IDF build: Class B host build for the ADS131 scaffold is recorded on the issue #35 pull request. That build is not hardware validation.
- On-target and HIL testing: not run.
