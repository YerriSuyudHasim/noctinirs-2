# LED host duration and frame-rate control

Owner direction,2026-10-27: remove the advisory2s engineering ceiling and the
one-accepted-START-per-boot restriction; allow every integer Fs from2 through10Hz.
This replaces the original issue33 duration/attempt constraints and the earlier
15s proposal. Agent03 Firmware Developer, Codex/OpenAI,
session agent03-host-control-20261027. Base23eb04217463772887cd5b755d5a0b3920a679a5
plus preserved uncommitted profile/tool changes and this source change.

## Resulting behavior

- `run --seconds` controls client duration. There is no firmware run-duration
  ceiling or boot attempt quota. Durations must be positive and finite; there
  is no arbitrary client maximum duration. Default client duration remains10s.
- START while running, stopping or faulted is rejected. After normal STOP and
  completed cleanup, another explicit START works on the same boot. Faults still
  require explicit clear-fault after cleanup; nothing restarts automatically.
- Fs is any integer2..10Hz, default5Hz, changed only while STOPPED. Firmware
  SET_FS, adapter start validation and CLI agree. INFO advertises fs_min=2,
  fs_max=10. Integer3,6,7,9Hz are included;0,1 and11+ are rejected.
- Advisory still requires twenty nominal30mA setpoints and the same board-specific
  complete fresh SHORT classification. Strict still treats ERROR as fatal.
  One-hot ON, unused-off and current ceiling remain. Runtime transport now uses
  the [V4-inspired async revision](LED_ASYNC_RUNTIME.md): full ON, compact normal
  OFF, synchronous100kHz boot/configuration, async800kHz advisory runtime.
- STOP/safe-off, host lease/link timeout, deadline detection, timer failure,
  OVERTEMP, OPEN/code3, I2C/readback and ERROR consistency checks remain active.
  The ISR still stops missed work and generation cancellation still prevents
  late reset release/rearm. The same task now owns an async I2C completion
  callback and depth1 driver queue; priority is22. No watchdog feed is added.

New advisory INFO fields:

```text
short_policy=BENCH_WARN;i2c=ASYNC@800000;duration=HOST;fs_min=2;fs_max=10;limit_s=0;single_run=0
```

`limit_s=0` explicitly means no duration cap. STATUS retains the compatible
limit_s/single_run fields at0. `led_attempt` marks an accepted current/last run,
not a lockout; `led_cap` remains0 and stop code2 is reserved for old records.
Per-frame warnings remain silent. Existing raw/freshness/first-cause telemetry
is preserved. The CLI reads existing INFO once before START: it accepts extended
runs on the new image and respects an older image's advertised cap/rate range,
reporting Owner build required instead of attempting an incompatible run.

Timing uses the existing1us GPTimer resolution. Half-slot duration is
floor(1,000,000/(40*Fs))us: requested Fs is nominal, with less than40us frame
quantization error. At10Hz a half-slot is2.5ms. Boot13 previously reported a
2.649ms maximum phase completion at Fs5; therefore successful physical10Hz timing
is not established by accepting the setting. Deadline protections remain active;
the async revision increases runtime SCL and reduces transactions while keeping
the same deadlines. Accepting Fs10 still does not prove physical timing.
Long duration does not establish electrical, thermal or optical qualification.

## Owner build required

**Owner build required,** in the existing ESP-IDF6.1 VS Code workspace, choose
**ESP-IDF: Build your project**. No Full Clean is required for these source changes.
The selected profile remains `main-board-short-advisory`; unrelated root settings
and existing uncommitted work are preserved. Equivalent manual command:

```powershell
idf.py -B build -D SDKCONFIG=sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup;sdkconfig.led-short-bench' -D NN2_EXPECT_LED_POLICY=BENCH_WARN -D NN2_EXPECT_LED_I2C_HZ=800000 -D IDF_TARGET=esp32s3 build
```

Expected message:

```text
NN2 board policy=BENCH_WARN; Fs2..10Hz; 30mA; duration=HOST; limit_s=0; single_run=0; physical qualification pending
NN2 LED transport=ASYNC; runtime SCL=800000; boot SCL=100000
```

The Owner's boot16..20 image already reports HOST duration and Fs2..10; it
uses the previous synchronous100kHz scan. This revision changes that transport. After the Owner installs the new image using their authorized procedure,
examples of client commands are:

```powershell
python tools/nn2ctl.py run --led --ack-short-warning --rate 5 --seconds 15
python tools/nn2ctl.py run --led --ack-short-warning --rate 10 --seconds 900
python tools/nn2ctl.py usb-status --serial-port COM11
```

Examples are separate runs, not a requirement to run both. `--seconds 900` means
15minutes; rate can be any integer2..10. The run command sends heartbeats and
requests STOP at the chosen duration. If a hard fault occurs, retain STATUS
before clearing/recovery and inspect its first cause. No repeated INFO/build
validation is required from the Owner; the client preflight is built in.

## Evidence and review limits

Owner boot15 reported STARTING -> STREAMING -> STOPPING -> STOPPED, fault0,
for the requested1s run on the previous BENCH_WARN image. This establishes the
reported command progression; no frame count or physical timing is inferred.

Lightweight author checks cover CLI15s/15min/>24h acceptance on the new capability,
legacy-cap compatibility, nonfinite/negative duration rejection, all nine frame
rates and range rejection, profile preservation and CMake-script expectations.
Production C native tests are updated for all byte-valued rates,15s/15min fake-time
progression, repeated runs on the same boot and fault/STOP/cleanup interlocks.
Host C syntax checks use MSVC /Zs only: no object generation or linking.

Native test execution and full firmware builds/link/size/map checks are NOT RUN
by the agent, per Owner instruction. Device/flash/energized tests are NOT RUN by
the agent. Exact changed-input hashes and actual lightweight results are in
[evidence](evidence/led-host-control-2026-10-27.json). Specialist delta review,
non-author verification and independent review remain pending; author source
checks are not physical qualification. Rollback restores the preceding reviewed source/configuration; no source edit changes firmware already on the device.
