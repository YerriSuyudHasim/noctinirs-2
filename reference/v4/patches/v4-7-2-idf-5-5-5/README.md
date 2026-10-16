# NoctiNIRS V4-7.2 — ESP-IDF 5.5.5 patch candidate

This is an isolated, unbuilt copy of the V4-7.2 firmware prepared for an
ESP-IDF patch-level uplift from 5.5.1 to 5.5.5.

## Status

- Source preparation: complete.
- ESP-IDF 5.5.5 build: not run.
- Static ELF/map review: not run.
- Hardware verification: not run.
- Active UDP Test 2: still blocked.

The authoritative 5.5.1 source and its recorded build evidence remain in the
parent `NoctiNIRSv4` project and V4 build manifest. Nothing in this directory
supersedes that evidence until a clean 5.5.5 build and the V4-7.2 Wi-Fi-idle
hardware gate pass.

## Intentional source differences

Only migration identity and build guarding differ from the parent V4-7.2
source:

- firmware version: `4.0.0-v4-7-2-idf-5-5-5-patch`;
- build ID: `V4-P7.2-IDF5.5.5-PATCH`;
- compile-time rejection of any ESP-IDF version other than 5.5.5;
- status output explicitly reports build and hardware as unverified;
- the build helper uses the active `IDF_PATH` instead of machine-specific
  paths and refuses to run unless `idf.py --version` reports 5.5.5.

ADS, PCA, optical timing, K policy, task priorities/affinities, Wi-Fi, UDP,
packet ABI, safety OFF, and scientific behavior are unchanged.

## Configuration provenance

- `sdkconfig.v5.5.1.reference` is the immutable pre-migration reference.
- Reference SHA-256:
  `D4885104DFDF133AC7CD2CD5FF8062A7D8B9D07420635AD238BFD232541F797E`.
- `sdkconfig` begins as an identical working copy for the 5.5.5 reconfigure.
- `sdkconfig.defaults` retains the locked project settings.

After building, preserve the generated `sdkconfig`, build log, size report,
ELF/map, and artifact hashes before flashing.

## User build

Open an ESP-IDF v5.5.5 PowerShell, change to this directory, and run:

```powershell
.\tools\build_clean.ps1
```

The script creates `build-v4-7-2-idf-5-5-5`. It intentionally performs no
flash or monitor action.

See `IDF_5_5_5_MIGRATION_AUDIT.md` for the framework delta and required
post-build checks.
