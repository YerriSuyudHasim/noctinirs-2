# Prompt 4 Classification and Frame Flow

## Data path

```text
DRDY GPIO ISR
  capture drdy_timestamp_us + sequence + session/optical snapshot
            |
            v
ADC task on core 1
  one 18-byte SPI frame
  validate SPI / CRC / status
  publish immutable raw record
            |
            v
raw sample SPSC ring (1024)
            |
            v
optical processing task on core 0 -- sole SCAN consumer
  merge raw records with immutable slot timing records
  classify by drdy_timestamp_us using half-open boundaries
  retain newest contiguous integrity-valid tail up to K=10
  arithmetic mean for CH0..CH3
  calculate I_corrected = I_LED_ON - I_LED_OFF
            |
            v
20-slot frame builder
  slot masks + validity + reason
  pair 735/850 for 10 physical sources
            |
            v
completed-frame SPSC ring (16)
            |
            v
single frame-sink task
  reject stale session/config
  copy latest-frame snapshot
  Prompt 5 will encode/transmit
```

## Boundary decision order

For a finalized timing record:

```text
[guard_start, slot_end)                         -> GUARD reject
wrong session                                  -> STALE reject
invalid/uncommitted timing                     -> AMBIGUOUS reject
[slot_start, on_commit)                        -> TRANSITION reject
[off_request, off_commit)                      -> TRANSITION reject
[on_commit, on_stable)                         -> SETTLE reject
[off_commit, off_stable)                       -> SETTLE reject
[on_stable, off_request)                       -> stable LED ON
[off_stable, guard_start)                      -> stable I_LED_OFF
anything else                                  -> AMBIGUOUS reject
```

The upper endpoint is always excluded. For example, a sample exactly at
`off_request` is transition, not ON; a sample exactly at `guard_start` is guard,
not `I_LED_OFF`.

## Ownership transfer

No completed frame contains pointers to mutable processing scratch. The
processing producer copies one full frame into the SPSC ring. The frame-sink
consumer copies it into a generation-guarded latest snapshot, then advances the
ring tail. Session/config mismatch is rejected before publication.
