# Prompt 2 Real-Time Review

## ISR safety

DRDY ISR call graph:

```text
GPIO falling edge
  -> common GPTimer raw count
  -> native 32-bit sequence/head/tail operations
  -> static compact event write or overflow flag
  -> direct task notification
  -> return
```

No SPI transaction is started in ISR. No log, heap, mutex, I2C, UDP, frame
processing, reset or delay is present. The timebase task/ISR accessor and the ISR
itself are marked for IRAM; target-map verification remains required.

## SPI transaction review

```text
6 words x 24 bits = 144 bits = 18 bytes
144 / 8 MHz = 18 us ideal SCLK time
125 us nominal DRDY period
```

CS is hardware-controlled by the SPI device for the whole transaction. A single
preallocated polling transaction is reused. `spi_duration_max_us` measures the
software-visible transfer duration. Hardware validation must confirm CS width,
SCLK mode/frequency and DRDY-to-CS latency.

## SPSC ordering

Producer:

```text
load own head (relaxed)
load consumer tail (acquire)
check capacity
write complete record
store head+1 (release)
```

Consumer:

```text
load own tail (relaxed)
load producer head (acquire)
copy complete record
store tail+1 (release)
```

Head and tail are monotonic 32-bit counters; array indexing uses power-of-two
masks. Full means `head-tail >= capacity`. Full never overwrites data.

## START/STOP race review

- All public START/STOP requests serialize through a static mutex and four-item
  static command queue.
- The ADC task is the sole lifecycle owner.
- START disables capture before clearing rings and only enables DRDY after
  synchronization and stale-state clearing.
- STOP disables the source before ending publication, then waits for the sole
  raw consumer to acknowledge drain.
- ISR/task/rings/semaphores remain allocated. Stale callbacks therefore cannot
  dereference deleted objects.
- `session_epoch` increments before capture enable.
- A DRDY overflow raises a task-visible pending fault even when no event record
  could be published.

## Stack review plan

Configured:

```text
ADC task              4096 bytes
DARK diagnostic       6144 bytes
UDP control           6144 bytes
```

`uxTaskGetStackHighWaterMark()` is exported through `HEALTH`. No stack is yet
accepted as sufficient until hardware logs show a stable nonzero margin under
Wi-Fi/UDP load.

## Deliberately deferred

- no PCA9956B async callback;
- no GPTimer optical alarms armed;
- no 20-slot scheduler;
- no optical classification/averaging;
- no binary streaming.
