# Prompt 3 Timing Analysis

## Nominal schedule

```text
slot period                   5000 us
nominal ON request            slot_start
nominal OFF request           slot_start + 2500 us
slot guard start              slot_start + 4600 us
slot end                      slot_start + 5000 us
20 slots                      100000 us
frame rate                    10 Hz
ADS period                    125 us
```

## Atomic PCA LEDOUT transaction

The application sends seven payload bytes: one auto-increment LEDOUT register
selector and six LEDOUT data bytes. Including the I2C address byte and ACK bits:

```text
(1 address + 7 payload) x 9 = 72 clocked bits
400 kHz ideal wire time      = 180 us
1 MHz ideal wire time        = 72 us
```

START/STOP, bus arbitration, driver overhead, interrupt latency, and task wake
latency are not included in ideal wire time. Runtime uses callback timestamp
minus submit timestamp as the conservative measured latency.

## Configured conservative windows

Using a 250 us commit allowance:

```text
ON = 2500 - 250 - 500 = 1750 us
I_LED_OFF = 2500 - 250 - 500 - 400 = 1350 us
```

At an exact 125 us DRDY grid, phase-safe lower-bound interval counts are 14 and
10. However, Prompt 3 does not claim accepted K because real callback latency,
DRDY phase, scheduler handling, ADC integrity, and physical optical settling
have not been measured together.

The OFF target of 1250 us survives only while completion allowance is no larger
than 350 us. The current 250 us allowance therefore has 100 us design margin.
Any hardware result above the allowance must be reported and the timing design
revisited; K must not be silently lowered.
