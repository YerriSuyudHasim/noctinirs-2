# Final Timing Calculation

## ADS131M04

```text
CLKIN                     8.192 MHz
OSR                       512
sample rate               8.192 MHz / (2 × 512) = 8000 SPS
nominal DRDY period       125 us
frame words               status + 4 channels + CRC = 6
word length               24 bit
SPI frame                 144 bit = 18 byte
SPI SCLK                  8 MHz
ideal wire time           144 / 8 MHz = 18 us
nominal wire occupancy    18 / 125 = 14.4%
```

Actual driver, interrupt, cache, arbitration, and task-wakeup duration must be
measured; `spi_duration_max_us` captures runtime maxima.

## Optical frame

```text
20 slots × 5000 us = 100000 us/frame
100000 us/frame = 10 frames/s
nominal LED-OFF = slot start + 2500 us
ON budget / OFF budget = 2500 / 2500 us
```

Deadlines are absolute from frame origin, not chained from callback completion.

## PCA9956B LEDOUT transaction

Atomic command is one slave-address byte plus register pointer and six LEDOUT
bytes, represented by 72 SCL clocked bits including ACK positions.

```text
400 kHz ideal wire time   72 / 400000 = 180 us
1 MHz ideal wire time     72 / 1000000 = 72 us
```

The source uses a conservative 250 us commit design allowance. Completion
callback time is used as a conservative commit boundary and is not the exact
optical edge.

## Stable windows

```text
ON stable design window
= 2500 - 250 commit allowance - 500 settle
= 1750 us
= 14 nominal 125-us periods

I_LED_OFF stable design window
= 2500 - 250 commit allowance - 500 settle - 400 guard
= 1350 us
= 10.8 nominal periods
```

These divisions do not guarantee sample counts because DRDY phase and runtime
latency matter. K=10/10 remains the target; K=8/8 remains minimum-valid. Hardware
acceptance must measure the distribution and worst case.
