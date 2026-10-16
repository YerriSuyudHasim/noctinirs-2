# Paired LED-OFF protocol v2

## Why the old sequence failed

The previous live-view started a standalone `MODE DARK` acquisition, attempted to build a five-second median LED-off profile, and only then switched to `MODE SCAN`. When the DARK profile did not satisfy the fixed-K validity rule, the software stopped before SCAN, so the LEDs never lit.

A standalone profile also becomes stale when ambient light or electronic offset changes after the profile was recorded.

## Final acquisition model

For every optical slot, the firmware measures a local LED-OFF phase and a local LED-ON phase. Both are placed in phase records that share the same `session_id` and `frame_id`.

```text
previous LED OFF
    |
    +-- 1000 us settling guard
    +-- newest 5 contiguous LED-OFF samples
    |
current LED ON
    +-- 1000 us settling guard
    +-- 1400 us acquisition window
    +-- newest 5 contiguous LED-ON samples
    |
current LED OFF
```

The bridge emits a corrected frame only after both records arrive:

```text
I_corrected[frame, slot, PD]
  = I_LED_ON[frame, slot, PD]
  - I_LED_OFF[frame, slot, PD]
```

No previous LED-OFF record is reused. If either phase is absent, under-K, CRC-invalid, or has an unequal sample count, that slot is invalid.

## Why K=5 rather than K=6

At 4 kSPS, each conversion period is 250 us. A five-sample average provides useful noise reduction while leaving a full 1000 us settling guard for both ON and OFF states inside the fixed 5 ms slot. K=6 gives only a small additional averaging benefit but substantially reduces timing margin once LED-driver I2C latency and ADC/filter settling are included.

## Data-integrity rules

1. ADS131M04 register readback must match the intended 4 kSPS configuration.
2. Measured DRDY rate must be within ±5% of 4000 SPS before acquisition is allowed.
3. Samples must be CRC-valid.
4. Selected samples must have consecutive `drdy_sequence` values.
5. Exactly K samples must exist in both phases.
6. LED-ON and LED-OFF records must have the same session and frame ID.
7. Corrected intensity must remain signed. The software does not apply `abs()` to hide negative values.

A negative corrected value is therefore treated as a polarity, saturation, mapping, or optical-coupling problem that must be investigated rather than numerically concealed.
