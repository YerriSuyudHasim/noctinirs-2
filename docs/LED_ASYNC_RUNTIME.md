# LED async runtime for ESP-IDF6.1

Acting as Agent03 Firmware Developer, Codex/OpenAI, session agent03-async-20261028.
Owner direction: study V4 and adopt its async transaction approach and800kHz in
active NoctiNIRS2 using ESP-IDF6.1. Base HEAD23eb04217463772887cd5b755d5a0b3920a679a5
plus preserved uncommitted work. No firmware build/link, flash or device access.

## Implementation and boundaries

Historical references were read, never modified or built:
`reference/v4/components/nocti_v4_async_led/nocti_v4_async_led.c` and
`reference/v4/components/nocti_v4_i2c/nocti_v4_i2c.c`.
Adapted ideas: persistent transfer storage, full ON plus compact normal OFF,
one outstanding transaction, completion notifications, generation cancellation,
and drain before teardown. V4 was not copied wholesale; no ADC/scheduler tasks,
relaxed deadlines or claims of V4 qualification were imported.

- The existing LED owner remains core1 with6144-byte stack. Priority changes5
  to22. GPTimer still dispatches40 phases per frame at integer Fs2..10.
- Boot and preparation stay synchronous100kHz: reset, full all-off, register
  programming/readback, unused channels off. With outputs verified OFF, the
  owner recreates its exclusive I2C bus for async transactions, queue depth1,
  and registers an IRAM completion callback. There is no second bus user.
- Async submit success means accepted, not completed. The owner blocks on
  task notifications until DONE, cancellation or timeout; other tasks can run.
  The portable board I/O API still returns completed operations. This is an
  interrupt-driven transport, not a fully pipelined/multi-transaction scheduler.
- Static internal TX32/RX24 storage is used even when board callers use stack
  buffers. One pending transfer and its generation identify the completion.
  Read data is copied out only after successful current-generation completion.
- The callback records DONE/error, wakes the owner, and asserts reset on a
  current-run transfer error. It never submits more I2C, releases reset or
  rearms GPTimer. The installed IDF6.1 I2C ISR discards the callback's returned
  wake flag, so the callback explicitly calls portYIELD_FROM_ISR when needed.
  I2C ISR cache safety is selected explicitly, along with existing GPIO/timer
  IRAM settings. No local SDK patches or dependency changes are made.
- ON is still a full7-byte LEDOUT image with exactly one selected active channel.
  Only after verified preparation, normal OFF clears the selected LEDOUT byte
  using2 bytes. The portable driver retains full OFF as its default fallback.
  Reset and boot still clear every output. A failed/unknown state never resumes.
- Each EFLAG byte is still read while its selected channel is ON, before OFF.
  Frame health combines adjacent MODE1/MODE2 into one auto-increment read.
  EFLAG cannot be included in this burst and is not cached across epochs.
- STOP, lease/link timeout, OVERTEMP, OPEN/reserved EFLAG, readback, ERROR
  consistency/freshness, and the original phase completion deadlines remain.
  On STOP/fault, reset is asserted before drain. Drain or callback-unregister
  failure retains buffers/handles, reports bus unavailable, and denies START.
  A late completion can unblock cleanup but cannot enable outputs or count an
  edge. There is no duration ceiling, attempt quota or automatic restart.

## Explicit profiles and identity

| Profile | SHORT policy | Boot/config SCL | Runtime SCL |
| --- | --- | --- | --- |
| main-board-short-advisory | BENCH_WARN |100kHz synchronous |800kHz async |
| main-board-strict | STRICT |100kHz synchronous |400kHz async |

The generic Kconfig runtime default is400kHz.800kHz is chosen only by the explicit
advisory overlay/profile; selecting a profile patches only declared root keys,
backs up existing configuration, and retains unrelated settings. The normal
VS Code Build action uses root sdkconfig and build/. CMake asserts selected
policy, runtime clock and ISR safety against generated configuration.

The PCA9956B supports up to1MHz ([NXP datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9956B.pdf)),
but [Espressif's ESP32-S3 IDF6.1 documentation](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/peripherals/i2c.html)
specifies a400kHz maximum master SCL.800kHz is therefore the Owner-selected
engineering setting, not an officially supported S3 guarantee. Software is
prepared now; electrical timing and sustained Fs10 acceptance remain review gates.

Both profiles advertise `i2c=ASYNC@<runtime_hz>` in INFO. The text is statically
bounded to fit the256-byte protocol reply cache, including60 bytes of overhead.
Existing source/image identity fields remain; HEAD alone cannot identify dirty
source. No new image hash exists until the Owner builds.

STATUS adds cached `led_i2c_tx`, `led_i2c_done`, `led_i2c_err`, `led_i2c_stale`, `led_i2c_event`.
The first counts attempts at async submission; done counts callback completions,
err counts non-DONE callbacks, stale counts canceled-generation completions.
Event1=DONE,2=NACK,3=TIMEOUT;0 means no callback observed (or an unexpected ALIVE
event if done>0). TIMEOUT retains ESP_ERR_TIMEOUT. Submit errors remain in led_error/led_stop; tx-minus-done alone is not proof of
an active transfer. Existing led_missed, led_dispatch_us, led_completion_us,
MODE2/EFLAG and first-stop evidence remain. STATUS performs no hardware reads.
The existing1536-byte USB status capacity is retained.

## Timing budget, not measurements

At800kHz, counting9 clocks per address/data byte, excluding START/STOP setup,
clock stretching, actual SCL variation, driver/ISR/task/critical-section costs:

| Operation | SCL clocks | Ideal wire time |
| --- | --- | --- |
| Full ON, address plus7 bytes |72 |90us |
| EFLAG single-register read |36 |45us |
| Compact OFF, address plus2 bytes |27 |33.75us |
| MODE1/MODE2 adjacent read |45 |56.25us |
| Last OFF: EFLAG + OFF + mode burst |108 |135us |

A frame uses2745 clocks,61 transactions,3431.25us ideal wire time. At10Hz this is
3.43125% average bus occupancy. The previous100kHz full-OFF/split-health path
used36720us/frame and1800us for the last OFF. The unchanged10Hz half-slot budget
is2500us. These numbers explain headroom; they do not establish measured WCET
or prove the earlier fault267 was a deadline rather than another hard fault.

## Owner build required

**Owner build required**, using the existing ESP-IDF6.1 PowerShell terminal:

```powershell
python tools/nn2_board_profile.py select main-board-short-advisory
idf.py -B build -D SDKCONFIG=sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup;sdkconfig.led-short-bench' -D NN2_EXPECT_LED_POLICY=BENCH_WARN -D NN2_EXPECT_LED_I2C_HZ=800000 -D IDF_TARGET=esp32s3 build
```

Alternatively, run the selector once, then **ESP-IDF: Build your project** in
VS Code. Ordinary Build regenerates changed Kconfig/CMake inputs; Full Clean
is not required. Expected configuration messages:

```text
NN2 board policy=BENCH_WARN; Fs2..10Hz; 30mA; duration=HOST; limit_s=0; single_run=0; physical qualification pending
NN2 LED transport=ASYNC; runtime SCL=800000; boot SCL=100000
```

Expected resulting INFO includes `short_policy=BENCH_WARN;i2c=ASYNC@800000`,
`duration=HOST;fs_min=2;fs_max=10;limit_s=0;single_run=0`.
This procedure builds only. It is not an agent flash/energization instruction.

## Verification, risks and recovery

Actual lightweight results and source fingerprint are recorded in
[evidence](evidence/led-async-2026-10-28.json). MSVC /Zs checks the portable board,
production owner and native fault-injection source in both policies. Xtensa GCC
-fsyntax-only checks the production owner against actual installed IDF6.1 headers
and both runtime clock settings; it generates no object and invokes no linker.
Python communication/profile tests and CMake script tests exercise client and
configuration behavior. Native executable tests were extended for deferred
completion, NACK/TIMEOUT, cancellation during wait, retained buffers after failed
drain, late callback and failed callback registration/unregistration. Native C
test execution is NOT RUN because the Owner prohibited linking. These are author
checks, not independent verification or on-target timing evidence.

Firmware builds, link/size/map checks and flash/device/energized tests: NOT RUN
by agent. Remaining gates: Owner build; RTOS review of priority/ISR/drain/lifetime;
Hardware review of800kHz and board-specific SHORT response; non-author verification;
independent review; authorized on-target sustained timing/error evidence. No merge
or release. If800kHz proves unsuitable, retain async/compact transactions at400kHz
by changing only the runtime clock and its matching profile expectation under
review; switching to STRICT also changes fault policy and is not equivalent.
Rollback can restore the prior source/configuration using Git and profile backups.
No source edit changes an already flashed image; a hard fault never auto-restarts.
