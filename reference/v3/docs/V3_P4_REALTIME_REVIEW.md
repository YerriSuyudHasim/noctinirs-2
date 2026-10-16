# NoctiNIRSv3 Prompt 4 Real-Time Review

## Result

```text
Source implementation                   COMPLETE
Host Clang syntax/type/format gate       PASS: 11/11 translation units
Prompt 4 logic test                      PASS
Source-policy scan                       PASS
ESP-IDF 5.5.1 target build               NOT RUN: toolchain unavailable
Hardware test                            NOT RUN: board unavailable
```

## Classification review

- Uses ISR `drdy_timestamp_us`, not SPI completion time.
- Uses half-open intervals at every transition.
- Transition, settle, guard, stale, and ambiguous samples are rejected and
  separately counted.
- Invalid newest samples break the representative tail; older samples are not
  substituted.
- 64-bit mean accumulators and corrected-subtraction intermediate avoid
  arithmetic overflow in reviewed ranges.

## Ownership and memory-order review

- Raw ring has one active consumer selected by mode: ADC diagnostic in DARK,
  optical processing in SCAN. Prompt 4 explicitly prevents the processing task
  from consuming DARK samples.
- Scheduler→processing timing and processing→frame-sink frame paths are static
  SPSC rings using release/acquire head/tail publication.
- Processing-only scratch is not shared.
- Completed frames are copied; no scratch pointer escapes.
- Frame-sink owns frame-ring tail and latest-frame publication.
- START/STOP obtains explicit processing and frame-sink reset acknowledgements.

## Lifecycle review

- Objects/tasks are persistent and created before START.
- START increments config generation, resets processing/frame ownership with
  acknowledgements, then starts ADC and establishes a new session.
- STOP invalidates scheduler generation, waits for force-OFF completion,
  disables/drains ADC, then acknowledges processing and frame-sink reset.
- Session/config filters prevent stale frames from becoming current.
- Recovery does not resume mid-frame.
- `RESET_COUNTERS` is stopped-only and preserves lifecycle epochs.

## Error-policy review

Single SPI/CRC/status failures remain sample-level integrity breaks and are
published so slot selection can reject the tail. Consecutive thresholds stop
the session. Ring corruption, DRDY stop, repeated scheduler/stale-callback
violations, and inability to ensure LED OFF are fatal.

Completed-frame ring overflow is a recoverable frame drop in Prompt 4 because
raw acquisition and LED safety are unaffected; Prompt 5 must convert this into
an explicit network-backpressure policy.

## Remaining target-dependent review

- ESP-IDF v5.5.1 API/link/Kconfig compatibility;
- IRAM/internal-DRAM linker placement;
- actual I2C/GPTimer callback cores;
- physical LED transition versus conservative completion timestamp;
- measured K distributions and frame qualities;
- stack watermarks on hardware;
- GPIO35–37 electrical availability;
- 100-cycle and fault-injection logs.
