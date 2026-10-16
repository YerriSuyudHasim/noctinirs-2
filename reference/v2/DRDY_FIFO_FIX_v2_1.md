# NoctiNIRS paired v2.1 — ADS131M04 DRDY/FIFO fix

## Root cause

The previous startup test counted GPIO falling edges for one second while no
ADS131M04 frames were read. This is not a valid sample-rate test because the
ADS131M04 output path contains a two-deep FIFO and DRDY behavior changes after
a pause in host reads. The pause also left the FIFO full before acquisition.

The frame reader then waited up to 500 us for DRDY to return high after every
SPI frame. When a second FIFO sample was pending, DRDY legitimately remained
low; the 500-us wait prevented the host from draining the FIFO and reduced the
observed throughput to about 1230 frames/s.

## Changes

- Added `nocti_fnirs_sync_conversions()` using a 5-us SYNC/RESET pulse.
- Synchronize before active-rate validation and before every acquisition START.
- Replaced passive edge counting with 512 complete, continuous frame reads.
- Rate is calculated from ISR timestamps and reports period range, sequence
  gaps, duplicate sequences, CRC failures, and inferred CLKIN.
- Reduced the post-frame DRDY-high guard from 500 us to 20 us. A pending FIFO
  sample is consumed immediately instead of waiting.
- Precision remains strict: firmware refuses paired K=5/5 acquisition unless
  the actively read stream is 3800..4200 SPS with no CRC or sequence faults.

## Expected healthy boot log

```text
ADS131 readback MODE=... CLOCK=0x0F0E GAIN=...
ADS131 active-read validation: frames=512 elapsed~127750 us rate~4000 SPS
period~250..250 us seq_gap=0 seq_dup=0 crc_bad=0 inferred_CLKIN~8192000 Hz
```

If the active-read result still remains close to 1230 SPS, the result is no
longer caused by passive DRDY counting or the 500-us FIFO stall. At OSR 1024,
1230 SPS implies an ADS131M04 CLKIN near 2.52 MHz and the physical clock source
should be measured at the CLKIN pin.
