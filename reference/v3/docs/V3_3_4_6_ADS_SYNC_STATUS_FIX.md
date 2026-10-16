# NoctiNIRSv3 3.4.6 — ADS SYNC STATUS Fix

## Hardware log diagnosis

The 3.4.5 log proves the SPI transport is healthy:

- ADS registers read back correctly (`ID=2405`, `MODE=0111`, `CLOCK=0F0A`).
- Measured data rate is exactly 8000 SPS.
- DRDY period is 124–125 us.
- `crc_bad=0` and `spi_err=0`.

Startup was rejected only because one frame reported `status_err=1`. The previous implementation treated `F_RESYNC` (STATUS bit 14) as a hard fault. A deliberate SYNC/RESET pulse is asynchronous to CLKIN and can legitimately set `F_RESYNC` once when the ADC realigns its digital filters.

## Revised policy

1. After each host-issued SYNC, discard three complete DRDY/SPI frames.
2. Accept at most one `F_RESYNC` indication during this bounded discard window.
3. Start rate measurement or optical sample delivery only after the discard window.
4. Treat any later/repeated `F_RESYNC` as an ADS status error.
5. Continue treating LOCK, REG_MAP, CRC_ERR, RESET, CRC type mismatch, word-length mismatch, CRC mismatch, SPI failure, and continuity loss as errors.

This change does not weaken steady-state integrity checks and does not modify the validated V2-derived SPI or I2C bus configurations.
