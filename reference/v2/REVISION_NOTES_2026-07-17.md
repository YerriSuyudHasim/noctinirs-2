# NoctiNIRS paired LED-OFF revision — 2026-07-17

## Firmware

- Added fNIRS config payload v2 with separate `k_on`, `k_off`, and phase timing metadata.
- Added paired phase flags to the fNIRS frame protocol.
- Added local per-slot LED-OFF acquisition instead of a standalone DARK profile.
- Added strict same-frame ON/OFF pairing semantics.
- Changed the precision timing to K=5 per phase with a 1000 us settling guard.
- Added contiguous-DRDY enforcement; CRC failures and sample gaps are not backfilled.
- Added hard ADS131M04 register and measured DRDY-rate validation.
- Added status/health diagnostics for ON and OFF under-K counts, minimum sample counts, and measured dark duration.
- Preserved explicit DARK mode as an all-LED-off diagnostic.

## Host software

- Added paired phase assembler keyed by UDP session and fNIRS frame ID.
- Computes `I_corrected = I_LED_ON - I_LED_OFF` only after a complete valid pair is available.
- Removed the automatic five-second DARK profile stage.
- Starts SCAN/SINGLE directly, so LEDs light immediately in normal acquisition.
- Rejects missing, under-K, CRC-invalid, unbalanced, or stale phase data.
- Exposes LED-ON, LED-OFF, corrected intensity, and per-phase sample counts to the UI.
