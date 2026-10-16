# Historical firmware references

The directories `v2/`, `v3/`, and `v4/` are sanitized snapshots of the original local NoctiNIRS firmware folders. They exist for analysis, comparison, and traceability. They are not active firmware and are not included by the root ESP-IDF build.

## Copy exclusions

- all nested `.git` metadata;
- generated `build/` trees (v4 contained one);
- downloaded dependency trees and local environments such as `managed_components/`, `.espressif/`, `.pio/`, `node_modules/`, virtual environments, caches, `dist/`, and `out/`.

No nested Git repositories were present in the three sources during preparation.

## Sanitization record

The source folders were not edited. In the copied v2 snapshot only, non-placeholder Wi-Fi literals were replaced with `REFERENCE_WIFI_SSID` and `REFERENCE_WIFI_PASSWORD` in:

- `v2/components/nocti_wifi/nocti_wifi.c`
- `v2/main/app_main_fnirs_test.c`
- `v2/all_code.txt` (an aggregate copy containing the same code)

No private-key markers or secret-looking filenames were found in the copied trees. Keyword matches in Wi-Fi APIs and ESP-IDF certificate configuration are not, by themselves, credentials.

## Integrity policy

`MANIFEST.sha256` records every file in this directory except the manifest itself. `tools/verify-reference.ps1` fails if a file changes, disappears, or is added. This is tamper detection, not access control.

Do not change a historical version during normal firmware work. Authorized snapshot maintenance must document provenance and exclusions, regenerate the manifest, and receive independent review.

