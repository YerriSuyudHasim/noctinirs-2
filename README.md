# NoctiNIRS 2

NoctiNIRS 2 is a clean ESP32-S3 firmware project starting at **v0.1.0**. It is governed by the separate private `ai-engineering-hub` repository. The active firmware is intentionally a minimal scaffold; no NoctiNIRS v4 implementation has been copied into it.

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
├── CMakeLists.txt
├── sdkconfig.defaults
└── VERSION
```

Only the root build definition and `main/` form the active firmware. Files under `reference/` are evidence and design input, not build inputs.

## Toolchain baseline

- Target: ESP32-S3
- Framework: ESP-IDF v6.1
- RTOS: FreeRTOS as supplied by ESP-IDF
- Initial firmware version: v0.1.0

ESP-IDF is not bundled with this repository. With ESP-IDF v6.1 activated:

```powershell
idf.py set-target esp32s3
idf.py build
```

The current scaffold only logs its version and an explicit warning that hardware behavior is not implemented.

## Historical references

The v2, v3, and v4 trees were copied from local source folders without nested Git metadata. Generated v4 build output was excluded. Wi-Fi literals found in v2 were replaced with non-secret placeholders in the copied snapshot; the originals were not changed. See `reference/README.md` for the exact sanitization scope.

Reference files are protected by policy and a SHA-256 manifest. Run:

```powershell
pwsh -File tools/verify-reference.ps1
```

A directory name cannot enforce immutability. Any intentional reference update therefore requires explicit human authorization, a manifest refresh, and review separate from active firmware work.

## Governance and workflow

Read `AGENTS.md` before changing this repository. Normal work follows issue → branch → pull request → CI → independent review → human merge approval. Hardware, timing, power, sensor, RF, and safety claims require evidence from the applicable test class; a successful build is not hardware validation.

The relationship to `ai-engineering-hub` is documentation and governance only—there is no Git submodule. The draft registry record must not be copied into the hub until the private GitHub repository URL and human owners are verified.

## Current status

- Local working tree prepared.
- Private GitHub publication: pending human confirmation.
- Hub registry entry: drafted, pending verified repository URL and owner fields.
- ESP-IDF build: not run because ESP-IDF is not available in the current shell.
- On-target and HIL testing: not run.
