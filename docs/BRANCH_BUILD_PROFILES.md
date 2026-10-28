# Build the profile stored in the selected branch

The default VS Code Build action on `feat/35-ads131-acquisition` now builds
communication + the real LED scheduler + ADS131M04 acquisition. Switching to
`feat/33-board-bringup` restores that branch's existing LED workspace settings.
The checkpoint's firmware and history are not modified by this change.

## Owner workflow

1. Check out the desired branch with a clean tracked working tree.
2. Let VS Code reload the workspace settings from that branch, then click its
   ESP-IDF **Build** button. No manual menuconfig selection is needed.
3. On feat35 the configure output must say
   `NN2 branch profile validated: ads131-acquisition`.

Feat35 paths:

- Profile definition: `config/active-build-profile.cmake` (tracked).
- Build cache/image: `build/ads131-acquisition/` (ignored).
- Generated configuration: `build/profile-configs/ads131-acquisition/sdkconfig`
  (ignored, distinct from the old root LED sdkconfig).
- Compilation database: `build/ads131-acquisition/compile_commands.json`.

First compilation in the new directory can be slow because it compiles the SDK
again. Subsequent Build actions reuse it. A restart does not change the selected
profile. Full Clean discards compilation results, not the versioned profile.
Do not repeatedly clean just because a build takes time.

CLI equivalent from an ESP-IDF terminal:

```powershell
$env:IDF_PY_BUILD_JOBS = "3"
idf.py -B build/ads131-acquisition -DNN2_USE_BRANCH_PROFILE=ON build
```

This selects the profile stored in the source branch, not a branch-name lookup.
Future development branches should update the profile identifier, overlay list,
required generated options and VS Code paths together when their default changes.
Branches derived from feat35 inherit acquisition until they change that contract.
Historical branches retain the settings committed in their own trees.

## Configuration ownership and stale-cache protection

`NN2_USE_BRANCH_PROFILE=ON` applies the versioned overlays before ESP-IDF project
configuration and chooses a generated sdkconfig by profile identifier. An old root
sdkconfig, SDKCONFIG cache entry or inherited defaults list cannot select LED-only
mode for this build. After generation, CMake rejects missing ADC/LED/communication,
wrong LED clock, incompatible flash/profile options or PSRAM rather than silently
producing a different image. User changes within this profile's generated config
remain possible; conflicting required keys must be corrected or that config backed
up and regenerated. No existing config is deleted automatically.

Generic CLI and CI matrix builds remain explicit: branch selection is opt-in via
the versioned VS Code CMake argument or the CLI flag above. To build fake,
diagnostic or other explicit overlays in a reused cache, pass
`-DNN2_USE_BRANCH_PROFILE=OFF` with their SDKCONFIG/SDKCONFIG_DEFAULTS arguments.
The existing manual LED selector also explicitly disables the branch default;
it is an override, not a required step in the normal checkout-and-Build workflow.
Do not carry locally modified tracked workspace settings across branches and
expect them to represent the destination branch's committed defaults.

## Scope and verification

Accountable role: Firmware Developer. Scope is build/profile selection and stale
configuration rejection, with host tests and target compilation. No application
algorithm, ADC register, pin, task ownership or historical source changes.
Acquisition keeps Fs >=2 Hz, LED runtime 800 kHz and power transactions 100 kHz.
The acquisition overlay retains the existing 16 MB/DIO image configuration; this
does not establish the physical board's flash geometry or qualify hardware.

Required review: build/integration and independent Verification; existing
Hardware/RTOS review remains required for the acquisition firmware itself.
Acceptance: normal VS Code configuration selects acquisition, root LED config is
preserved, generated mismatch is rejected, explicit CI profiles remain usable,
and the affected ESP32-S3 acquisition image links. Evidence and exact commit
identity are recorded separately after the verification run. No flash, board
operation, merge or release is part of this task.

Rollback: revert the build-selection commit to restore the previous workspace
settings. Generated directories can be retained; they do not change source or the
old LED checkpoint. Reference material: existing active overlay/profile selector
and ESP-IDF v6.1 configuration code; no V3/V4 adaptation in this change.
