# NoctiNIRS paired acquisition v2.3 — absolute optical deadlines

## Problem corrected

The previous scheduler tied LED-OFF to work completed inside the slot callback.
OFF averaging and frame assembly could consume most of the nominal 2.6-ms OFF
budget. Hardware logs showed `off_dark_min_us` around 227 us, `min_n_off=0`, and
nearly every LED-OFF slot marked UNDER-K.

## Acquisition timeline

Each 5-ms slot now follows fixed physical transition deadlines:

- `tick + 0 us`: begin the selected LED-ON I2C transaction.
- `actual ON I2C STOP + 1000 us`: earliest accepted `I_LED_ON` sample.
- `tick + 2500 us`: begin the LED-OFF I2C transaction.
- `actual OFF I2C STOP + 1000 us`: earliest accepted `I_LED_OFF` sample.
- next `tick + 0 us`: end of the conservative LED-OFF window.

The minimum accepted sampling interval is 1250 us. At 4 kSPS this guarantees at
least five conversions for any DRDY phase. A slot is aborted rather than silently
using fewer settled samples when an I2C transaction or scheduler dispatch is late.

## Timing-critical and noncritical work

The scheduler callback now performs only:

1. the physical LED transition;
2. timestamp capture;
3. a nonblocking queue operation.

ADC-window search, contiguous-sample validation, averaging, frame assembly, and
stream writes run in a separate task on core 0. The ADS131 producer remains on
core 1 at priority 18 and the optical scheduler at priority 17, allowing every
250-us DRDY frame to drain while retaining absolute LED deadlines.

## LED-driver path

Normal slots use one LEDOUT register write instead of a six-register atomic burst.
Every slot still has an explicit OFF transition. `cleanup_missed_slot_end=true`
ensures that an old channel is switched OFF before a different channel can switch
ON. Unknown state or I2C failure still invokes all-off/hardware-reset fail-safe.

## ADC history protection

The processing queue stores up to 64 slot jobs (320 ms at 200 slots/s). The ADC
ring stores 2048 samples (512 ms at 4 kSPS). Before processing a job, the task waits
up to 3000 us until the ADC producer has committed through the optical endpoint.
It then verifies that the oldest required timestamp is still present in the ring.
No stale or newer optical state is substituted when history is unavailable.

## Files changed

- `main/app_main_fnirs_test.c`
- `components/nocti_scheduler/nocti_scheduler.c`
- `components/nocti_scheduler/include/nocti_scheduler.h`

The paired-v2 binary wire format is unchanged, so the existing paired liveview
software remains compatible.

## Expected healthy runtime

After `MODE SCAN` and `START`, the important counters should converge to:

```text
partial=0
underk_on=0
underk_off=0
min_n_on=5
min_n_off=5
on_stable_min_us>=1250
off_stable_min_us>=1250
process_drop=0
process_wait_timeout=0
sched_missed=0
sched_abort=0
crc_bad=0
led_err=0
```

`off_dark_min_us` should be much larger than the previous 227 us and normally near
the 2.5-ms OFF half-slot minus the LED-OFF transaction and tick dispatch latency.

If a physical transition cannot leave the full 1000-us settling interval plus the
1250-us sample window, the firmware deliberately reports `sched_abort`/partial
instead of reducing K or accepting transition-contaminated data.
