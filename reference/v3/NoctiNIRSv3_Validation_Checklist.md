# NoctiNIRSv3 Validation Checklist

This checklist separates source completion from target build and hardware
acceptance. Do not mark a hardware item complete without preserving the log,
firmware hash, sdkconfig, board revision, module part number, and test conditions.

## A. Reproducibility

- [ ] Record final archive SHA-256.
- [ ] Record Git commit or immutable source hash.
- [ ] Confirm ESP-IDF exactly v5.5.1.
- [ ] Archive generated `sdkconfig`, linker map, size report, and build log.
- [ ] Record ESP32-S3 module exact marking and NoctiBoard PCB revision.
- [ ] Verify GPIO35/36/37 are not physically reserved by in-package/module PSRAM.

## B. Static and build gates

- [x] Host syntax/static review passes 11/11 translation units.
- [x] Stream-v1 CRC/layout/sequence host test passes.
- [x] Python validation tools compile.
- [ ] `idf.py fullclean build` passes without compiler errors.
- [ ] Important compiler warnings are zero or documented.
- [ ] Linker map confirms intended ISR/callback code/data placement.
- [ ] Firmware flashes and boots without reset loop or watchdog event.

## C. Safe boot

- [ ] PCA9956B RESET# is asserted before bus/runtime initialization.
- [ ] All LED outputs remain OFF through reset and boot.
- [ ] ADS does not start optical SCAN automatically.
- [ ] STATUS distinguishes Wi-Fi, network, peer, acquisition, stream, and fault.
- [ ] 400 kHz profile is active by default.

## D. Test 1 — DARK, Wi-Fi OFF, 10 minutes

- [ ] Run a dedicated no-Wi-Fi validation build/profile or otherwise prove radio
      and network tasks are not running; the normal final build boots SoftAP.
- [ ] `adc_missed=0`.
- [ ] `adc_sequence_gap=0`.
- [ ] `crc_bad=0`.
- [ ] `drdy_event_overflow=0`.
- [ ] `adc_ring_overflow=0`.
- [ ] Save period min/max/average and SPI duration max.

## E. Test 2 — DARK, Wi-Fi ON + UDP, 10 minutes

- [ ] Repeat DARK with SoftAP, commands, and UDP traffic.
- [ ] ADC integrity is no worse than Test 1.
- [ ] Network failure/drop does not stop acquisition.
- [ ] Task stack watermarks remain stable and noncritical.

## F. Test 3 — SCAN 20 slots, 10 Hz, 10 minutes

- [ ] `sched_missed=0` after startup transient.
- [ ] `sched_abort=0`.
- [ ] `partial_frames=0`.
- [ ] `invalid_frames=0`.
- [ ] `underk_on=0`.
- [ ] `underk_off=0`.
- [ ] `min_n_on>=8`.
- [ ] `min_n_off>=8`.
- [ ] Normal sample counts cluster around target 10/10.
- [ ] Transition, settling, and guard samples are rejected, not averaged.
- [ ] Logic analyzer confirms actual LED command/commit timing.

## G. Test 4 — Wi-Fi stress

- [ ] Continuous UDP capture runs without blocking RT tasks.
- [ ] Repeated STATUS/HEALTH/TIMING commands do not produce ADC/scheduler misses.
- [ ] Disconnect/reconnect increments network drops as expected without faulting.
- [ ] No progressive queue/ring depth growth.

## H. Test 5 — I2C fault injection

- [ ] Single NACK invalidates the affected slot.
- [ ] Force-OFF path is invoked.
- [ ] RESET#/recovery task does not run in normal slot path.
- [ ] Recovery verifies MODE1/MODE2 and all LEDOUT registers OFF.
- [ ] No deadlock or stale completion is applied to a new session.
- [ ] Repeated fault threshold stops/faults the session safely.

## I. Test 6 — START/STOP 100 cycles

- [ ] No use-after-free or task/object deletion race.
- [ ] No stale callback accepted after epoch change.
- [ ] No LED remains ON after STOP.
- [ ] Old completed frames never appear in a new session.
- [ ] Session/config generations advance consistently.
- [ ] Heap, task count, and stack watermarks remain stable.

## J. Test 7 — 400 kHz vs experimental 1 MHz

- [ ] Use separate, archived builds for each profile.
- [ ] Compare I2C wire time and completion latency distributions.
- [ ] Compare NACK, timeout, callback core, scheduler lateness, and K ON/OFF.
- [ ] Capture SCL/SDA rise time, high/low time, ACK, STOP, and bus recovery.
- [ ] Keep 400 kHz as default unless 1 MHz passes the complete hardware matrix.

## K. Test 8 — 30–60 minute soak

- [ ] Integrity counters remain zero.
- [ ] No progressive latency or ring-depth growth.
- [ ] Stack watermarks stabilize.
- [ ] No watchdog, brownout, reset, or Wi-Fi degradation.
- [ ] LED-off fail-safe remains effective after forced faults and recovery.

## L. Final acceptance classification

- [ ] Build verified.
- [ ] Static review verified.
- [ ] Hardware test partially verified, with listed tests and logs.
- [ ] Acceptance verified only after all required tests pass.
