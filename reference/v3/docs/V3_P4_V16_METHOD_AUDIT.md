# Prompt 4 Audit of v16 Representative Method

Source reviewed:

```text
NOCTINIRSv2/main/app_main_fnirs_test.c
function: fnirs_collect_and_average()
```

The v16 function:

- scans newest to oldest;
- limits selection to a fixed K;
- stops when the timestamp is older than the interval;
- does not substitute older samples after CRC/timing failure;
- stops at a DRDY sequence discontinuity;
- computes an arithmetic mean over four ADC channels with 64-bit accumulators.

Prompt 4 preserves these semantics. Two intentional clarifications are made:

1. intervals are strictly half-open `[start, end)` as required by the Prompt 4
   contract; v16 used an inclusive upper test;
2. integrity now includes explicit SPI result and ADS status in addition to CRC
   and sequence continuity.

This is a port-with-correction decision, not a new median/trimmed-mean design.
