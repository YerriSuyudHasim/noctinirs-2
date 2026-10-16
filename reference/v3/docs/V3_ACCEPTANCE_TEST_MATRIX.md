# NoctiNIRSv3 Acceptance Test Matrix

No test in this matrix is marked passed in the generated package because no
NoctiBoard or hardware log was available. Preserve actual logs and firmware
hashes when executing each procedure.

## Common setup

1. Build using ESP-IDF v5.5.1 and archive `sdkconfig`, map, size, and binary hash.
2. Record module marking, board revision, optical fixture, power source, and room
   light conditions.
3. Confirm GPIO35–37 compatibility before flash.
4. Boot and collect VERSION, STATUS, HEALTH, TIMING, REGS.
5. Use one client as UDP command/stream peer unless multi-client behavior is
   explicitly under test.

## Test 1 — DARK, Wi-Fi OFF, 10 minutes

The normal final firmware boots SoftAP. A true Wi-Fi-off comparison therefore
requires a dedicated validation build/profile that does not initialize Wi-Fi, or
an instrumented equivalent proving network tasks/radio are absent. This is a
manual comparison test, not covered by the normal network runner.

Target:

```text
adc_missed=0
adc_sequence_gap=0
crc_bad=0
drdy_event_overflow=0
adc_ring_overflow=0
spi_errors=0
```

## Test 2 — DARK, Wi-Fi ON + UDP, 10 minutes

```bash
python tools/nocti_acceptance_runner.py <ip> dark --duration 600
```

In parallel, repeatedly request STATUS/HEALTH and optionally capture binary UDP.
ADC integrity must remain equal to Test 1. Peer/network loss may increment
`network_drop` but must not stop DARK acquisition.

## Test 3 — SCAN, 20 slots, 10 Hz, 10 minutes

```bash
python tools/nocti_acceptance_runner.py <ip> scan --duration 600
python tools/nocti_udp_capture.py --bind 0.0.0.0 --port 3334 --duration 600
```

After startup transient target:

```text
sched_missed=0
sched_abort=0
partial_frames=0
invalid_frames=0
underk_on=0
underk_off=0
min_n_on>=8
min_n_off>=8
normal n_used approximately 10/10
```

Verify with logic analyzer that transition/settle/guard samples are excluded.

## Test 4 — Wi-Fi stress

```bash
python tools/nocti_acceptance_runner.py <ip> stress --duration 600
```

Generate continuous stream reception and repeated commands. Verify no new ADC,
CRC, ring, scheduler, or stale-callback error. Deliberately stop the receiver;
`network_drop` may rise while acquisition remains healthy.

## Test 5 — I2C fault injection

Manual hardware test:

1. Start SCAN at 400 kHz.
2. Introduce controlled PCA9956B NACK/disconnect/line fault.
3. Observe affected slot invalidation and immediate safe-OFF request/RESET#.
4. Verify recovery task performs reinit/readback outside normal slot path.
5. Confirm no deadlock, no stale completion applied to new epoch, and no LED
   remains ON.
6. Repeat to cross the configured consecutive-error threshold and verify safe
   session stop/fault.

## Test 6 — START/STOP 100 cycles

```bash
python tools/nocti_acceptance_runner.py <ip> startstop --cycles 100
```

Repeat with STOP deliberately timed around ON pending, OFF pending, guard, and
frame finalization. Verify no stale frame, stale callback, LED-on residue,
object leak, task-count growth, or session/config inconsistency.

## Test 7 — 400 kHz versus experimental 1 MHz

Create separate clean builds:

- default `CONFIG_NOCTI_V3_I2C_PROFILE_400KHZ=y`;
- experimental `CONFIG_NOCTI_V3_I2C_PROFILE_1MHZ_EXPERIMENT=y`.

Compare:

- SCL/SDA waveform, rise/fall and high/low timing;
- ideal/actual transaction duration;
- completion latency distribution and max;
- NACK/timeout/queue-full;
- scheduler lateness/miss;
- ON and `I_LED_OFF` K distributions;
- Wi-Fi stress behavior.

Do not promote 1 MHz to default based on build success alone.

## Test 8 — 30–60 minute soak

```bash
python tools/nocti_acceptance_runner.py <ip> soak --duration 3600
```

Capture health snapshots periodically. Integrity counters must remain zero,
stack/ring depth must stabilize, and there must be no progressive latency,
watchdog, reset, or unsafe LED state.

## Required evidence per test

- firmware/archive SHA-256;
- generated sdkconfig and build log;
- raw serial log;
- command replies and UDP capture;
- logic-analyzer file where applicable;
- before/after HEALTH snapshots;
- explicit pass/fail with deviations.
