> CURRENT UPDATE (2026-10-27): Owner selected single-cell LiPo, nominal 512 mA,
> cell maximum 4.20 V. REG02/REG04 now program 512 mA / 4176 mV with readback,
> retaining voltage-tolerance headroom. Prior statements below about preserving
> existing fast-charge current/voltage are superseded. See ADR-0004 local binding
> and ADR004_EVIDENCE.md for this source revision; no hardware result is claimed.

# Main-board power initialization and battery monitoring

> LED bring-up integration: the optional LED owner holds I2C0 throughout a scan.
> Due battery polls are deferred until it drains/releases the bus. The 300-second
> acquisition policy remains a due interval, not a guaranteed refresh during an
> uninterrupted LED scan. See ADR-0005. Default communication-only polling is unchanged.

## Scope and ownership

Owner request (2026-10-27): implement a dedicated ESP-IDF 6.1 board power library,
enable charging automatically at boot, and monitor the battery every 60 seconds
while idle and every 300 seconds during acquisition. Do not interfere with the
future LED driver. This is reversible local implementation; no hardware operation,
push, merge, release or SDK/dependency installation is performed.

Accountable implementation responsibility: firmware development in this local
session. Hardware and RTOS specialist reviews and independent review are pending;
this document does not claim a recorded governance role assignment. Issue/PR
registration and exact-head evidence remain required before integration/merge.

The existing dirty workspace is preserved. The implementation adds a source and
header to the existing `nn2_board` platform component, a pure application monitor
in `nn2_app`, and an abstract read port in `nn2_ports`. Core and application code
do not include ESP-IDF, GPIO, I2C or board headers. The communication composition
root wires the monitor to the board adapter. Fake/ADC diagnostic profiles do not
invoke power initialization.

Excluded: battery qualification, charge current/voltage selection, temperature
compensation/model tuning, fuel-gauge reset/quick-start, alerts/IRQs, OTG operation,
LED driver implementation, optical timing claims, and any physical testing.

## Current schematic evidence

Source: `Schematic_and_hardwareInfo/SCH_Thesis_main_2026-10-16.json`, Main_board.
This is schematic evidence, not confirmation of the assembled board revision.

| Signal | Trace |
|---|---|
| SDA GPIO15 | U1 IO15 (1255,-1470), wire gge58578, net_SDA label gge58566 |
| SCL GPIO16 | U1 IO16 (1255,-1460), wire gge58560, net_SCL label gge58548 |
| Shared SDA labels | wire gge58796 bridges SDA at (1035,-1275) to net_SDA at (1085,-1275) |
| Shared SCL labels | wire gge58793 bridges SCL at (1035,-1295) to net_SCL at (1085,-1295) |
| MAX17048 U9 | pin8 SDA through gge41138; pin7 SCL through gge41153 |
| BQ24295 U10 | pin6 SDA through gge43781; pin5 SCL through gge43766 |
| Charge enable GPIO8 | U1 IO8 (1255,-1430), wire gge55402, CE_BQ label gge55185; U10 pin9 through gge43160 |
| LED shared wires | PCA9956B pin37 SDA through gge24450 to net_SDA; pin36 SCL through gge24447 to net_SCL |

MAX17048 uses address 0x36 and BQ24295 uses 0x6B from their device interfaces.
Both are registered on I2C0 at a conservative selected 100 kHz, with external
pullups and 20 ms per transfer. Clock and timeout are implementation selections,
not measured bus timing or approved electrical limits. Verify pullups, actual
assembly, voltage domains, capacitance and a battery-specific charge procedure
before using this firmware on hardware.

## Boot and charger behavior

1. Existing `nn2_board_init_safe()` asserts PCA9956B RESET_LED low.
2. Power initialization preloads CE high before GPIO output configuration.
3. Create a synchronous I2C0 bus. Refuse an already-owned controller; never adopt
   someone else's possibly asynchronous bus.
4. Read MAX17048 VERSION (0x08, 0x001x) and BQ24295 REG0A part bits (110).
   Unexpected identities or transaction failures stop initialization.
5. For automatic enable, keep CE high, require the BQ charging safety timer to
   be enabled, clear REG05[5:4] (host watchdog only), and set REG01 charge mode
   (OTG_CONFIG=0, CHG_CONFIG=1). Preserve other configuration, including current,
   voltage, termination, safety timer and SYS_MIN. Do not replay register-reset
   or watchdog-reset command bits. Read back the resulting registers; CE goes
   low only after successful verification.
6. Release all power device handles and the bus, then start the battery monitor.
   The first monitor poll reads immediately; failures stop startup and request
   charging disabled. Normal boot then starts the existing communication stack.

This enables charging with the IC's existing current/voltage configuration. It
does not qualify that configuration for a particular cell, and is not a claim of
measured charge current. Disabling the host watchdog removes the need to inject
watchdog-kick transactions during LED acquisition; the charging safety timer and
hardware protections are not disabled. Software cannot guarantee CE behavior
before app_main, through reset/power failure, or when GPIO operations fail.

Any later startup/communication failure requests CE high and releases owned I2C
resources. Runtime telemetry failure retains the previous measurement and reports
an error; it does not change charge state or implement a battery safety policy.
Errors removing devices/deleting the bus retain the live handles for cleanup;
starting an LED phase after such an error is forbidden.

## Polling and bus ownership decision

Use synchronous I2C for these short, infrequent management reads. ESP-IDF 6.1
documents asynchronous master operation as experimental and restricts asynchronous
operation to one device on a bus. The installed driver's `master_bus->async_trans`
selects asynchronous execution for transfer calls at bus scope. Stack buffers in
the power driver are valid because no callbacks are registered and every transfer
completes synchronously. Do not transplant the V2/V3 callback field names.

The library does not scan addresses, explicitly reset the bus, install callbacks, create tasks,
keep a queue of transfers, register the LED device, or write LED registers. All
power APIs belong to the task that called init, currently app_main. No mutex is
needed for this single-owner contract; cross-task calls are rejected.
ESP-IDF itself can reset the controller FSM after a synchronous transfer error;
the exclusive power phase ensures no LED operation is pending during recovery.

The application monitor uses monotonic microseconds and attempts one read:

- immediately on its first eligible poll;
- every 60 seconds from the last attempt in idle mode;
- every 300 seconds from the last attempt in acquiring mode.

Switching mode uses the current interval relative to the same last attempt. A
closed I2C window postpones a due read without consuming its deadline. Reopening
the window performs one read, never a catch-up burst. Failed attempts retain
the last valid value/timestamp and wait one full interval before retrying.
Voltage and SOC are separate register reads, not an atomic hardware snapshot.
MAX17048 conversion is 78.125 microvolts/LSB and 1/256 percent/LSB; raw values are
retained and integer outputs rounded. Default gauge compensation is retained;
this is telemetry, not a validated SOC accuracy or battery-protection claim.

The board read port acquires I2C0, rechecks identities, reads VCELL/SOC/BQ REG08,
and removes its devices and bus before returning. It never publishes a partial
sample. Five sequential I2C reads each have a 20 ms configured transfer timeout;
100 ms is a nominal timeout budget, not a measured wall-clock upper bound.
Driver setup/teardown and RTOS scheduling also contribute latency.

**Future real acquisition must provide an exclusive idle-window grant.** The
LED driver must finish/drain all queued operations, remove devices, delete its
bus and yield I2C0 before the power port can run. Only after successful release
may LED recreate its own bus, including a separately reviewed asynchronous mode.
Power reads are forbidden for the entire LED-owned phase, not merely between
individual LED writes. Never map I2C1 onto the same SDA/SCL to evade ownership.

Current communication acquisition is simulated and has no LED bus owner, so its
composition root explicitly supplies an open window. On a future physical LED
implementation, the 300-second interval means 'due'; if no long enough idle
window exists, report stale telemetry and defer it. Concurrent uninterrupted
optical acquisition plus a strict five-minute battery deadline needs a separate
physical I2C bus/wiring or an approved acquisition pause and timing measurements.
Neither a mutex nor asynchronous transfers can remove shared-wire occupancy.

USB STATUS uses cached battery_valid, battery_uv, battery_mperm (milli-percent),
battery_age_ms and battery_error (NN2 status code) without I2C. battery_valid only
means a prior read succeeded; consumers must inspect age and the last error.
The STATUS buffer is 768 bytes and USB TX buffer 1024 bytes to accommodate the
added fields. Periodic samples use no NVS writes. No low-battery shutdown,
threshold, increased low-battery polling or cell-temperature policy is inferred.

## Acceptance and verification

Host/unit mock acceptance:

- Correct address/pin/synchronous configuration; CE high before configuration,
  identity check and verified enable; only intended BQ bits change.
- Reject absent/wrong devices, disabled safety timer, mismatched readback and
  injected boot-stage failures without a successful CE-low request.
- Correct VCELL/SOC conversion; partial read failure leaves output unchanged.
- Release blocks further power reads; an existing LED bus cannot be adopted;
  wrong-task calls cannot change hardware; cleanup errors retain ownership.
- Idle/acquisition interval edges, mode transitions, busy-window deferral,
  stale-value preservation, no immediate failure retry, monotonic-time checks
  and no catch-up bursts.

Tests live in `test/board_power`; build them with a native C11 compiler and CMake,
then run CTest. These use production board/application source against explicitly
limited mock IDF headers. They do not establish actual ESP-IDF linkage, electrical
correctness, watchdog timing, charging safety, or optical timing. Validation
results and exact source identity are recorded separately in BOARD_POWER_EVIDENCE.md.

Firmware build and all on-target/HIL/field tests: NOT RUN. Owner-operated build
must record actual IDF/toolchain versions, ESP32-S3 target, board revision, generated
configuration, warnings, binary/flash/RAM size and stack watermark. Hardware and
RTOS reviewers must approve power configuration and the future bus handoff.
Physical charging tests require a battery-specific approved procedure with
current/voltage/temperature limits, instrumentation and stop/power-removal steps.

Rollback: revert this task's power/monitor wiring and additions while preserving
pre-existing local edits. This does not change bootloader, partitions, NVS data,
secure boot, flash encryption, eFuses or reference snapshots. Firmware rollback
alone does not restore changed charger registers until reset/reconfiguration;
first disable CE and use the approved board recovery procedure.

## References and conflicts

- [ESP-IDF 6.1 I2C master API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/peripherals/i2c.html)
- Installed `C:/esp/v6.1/esp-idf/components/esp_driver_i2c/include/driver/i2c_master.h`
  and `i2c_master.c` (not changed).
- [MAX17048/MAX17049 datasheet, Rev 7](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX17048-MAX17049.pdf)
- [BQ24295 datasheet, Rev B](https://www.ti.com/lit/ds/symlink/bq24295.pdf)
- `reference/v2/components/nocti_board/nocti_board.c`: host watchdog disable and
  CE enable concept. Changed: identity/readback failures stop startup rather than
  logging and enabling anyway. No historical implementation copied wholesale.
- `reference/v3/components/nocti_v3_board/nocti_v3_board.c`: deferred LED-owned
  I2C concept; its async callback API is not the active SDK 6.1 API.

Earlier local communication notes forbade any LED-bus initialization. This
owner-requested change supersedes that blanket restriction only for short power
management phases on the physically shared wires; emitter reset remains asserted
and no LED device is registered. Reference pin constants are not authoritative.
