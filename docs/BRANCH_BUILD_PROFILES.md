# LED checkpoint branch build profile

On feat33, the normal VS Code ESP-IDF Build action now selects communication +
LED short-advisory at 800 kHz, with no ADS131 optical acquisition. The profile is
versioned in `config/active-build-profile.cmake`; no Git branch-name inference.
On feat35, its own versioned settings instead select LED + ADS131 acquisition.

Check out the desired branch with clean tracked settings, allow VS Code to reload
its workspace settings, then click ESP-IDF **Build Project**. Generic CMake Tools
status buttons/configure-on-open are disabled to avoid desktop compiler selection.
First compilation per profile can be
slow. Later builds reuse the profile's cache. Full Clean does not change which
profile the branch selects. No manual menuconfig step is required.

Feat33 cache/image: `build/led-bringup/`. Generated config:
`build/profile-configs/led-bringup/sdkconfig`. The old root sdkconfig is preserved.
Configuration output must say `NN2 branch profile validated: led-bringup`.

CLI: `idf.py -B build/led-bringup -DNN2_USE_BRANCH_PROFILE=ON build`.
Explicit CI/diagnostic profiles retain their old behavior; pass
`-DNN2_USE_BRANCH_PROFILE=OFF` when overriding a previously selected branch cache.
The existing manual LED selector also disables the branch default explicitly.

Scope: build selection only; no firmware source, hardware constants or historical
references changed. Accountable: Firmware Developer. Required review: integration
and independent Verification. LED runtime remains 800 kHz, power 100 kHz; existing
hardware qualification limits remain. Acceptance: the generated configuration
selects communication + LED, rejects incompatible profile options, and separates
this cache from feat35. No flash, device operation, merge or release. Rollback:
revert this build-selection commit; retained caches do not change source.

Shared build helper adapted from feat35 build-selection commit `10df540`, not
from historical firmware. Original LED firmware checkpoint remains at `d2f6d04`.

Verification on 2026-10-28: full ESP32-S3 configure/build/link PASS at `295c8b2`,
ESP-IDF6.1 / SDK `fff9895c82d744c7237be8847347bdd1b07c6643`, Xtensa GCC15.2.0.
LED image838816 bytes, communication +LED active, runtime800kHz, optical ADC option
absent. No compiler warning diagnostics; SDK configure notices remain. Existing
board-profile tests10/10 PASS, historical integrity319 files before/after.
The subsequent `1a63812` only changes generic CMake UI visibility and docs.
Detailed two-branch evidence is retained in feat35's PR75 and its
`docs/evidence/branch-build-profiles-2026-10-28.json`. No board/runtime tests run.
