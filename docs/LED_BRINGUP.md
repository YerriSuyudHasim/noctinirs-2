# LED-only bring-up candidate

> The strict profile faults on MODE2 ERROR. An optional, disabled-by-default
> board-specific SHORT advisory candidate is now implemented under
> `CONFIG_NN2_LED_SHORT_BENCH`. See [ADR-0006](adr/0006-bounded-short-warning-bench.md)
> and [the software handoff](SHORT_ADVISORY_HANDOFF.md). This is an engineering
> candidate; physical qualification, activation and independent approval remain pending.

The tested historical references are V2/V3; V4 is excluded as the baseline.
Default firmware simulates LEDs; real bring-up requires the separate LED overlay.
No candidate code changes the running device.

## Behavior

- Boot: emitters requested OFF; LED profile performs zero/readback/reset preflight.
- Explicit START: prepare/read back current configuration, then scan all 20 optical
  slots, one at a time. Physical PCA channels 0..9 and 14..23; 10..13 remain OFF.
- Default optical Fs=5 Hz, default current=30 mA per slot. Current is a nominal
  setpoint, approximately 29.925 mA after DAC quantization with 1 kOhm REXT.
- Strict profile allowed Fs={1,2,4,5,8,10}. Change while STOPPED only, retained until reboot.
- Strict bring-up current range=0..30 mA. Zero skips light in that slot; no simultaneous
  all-channel test. The global simulation cap of 50 mA is not enabled here.
- 5 Hz: frame200 ms, slot10 ms, nominal ON/OFF5 ms each. 10 Hz: frame100 ms,
  slot5 ms, nominal ON/OFF2.5 ms each. Wire/dispatch latency affects actual light
  edges; measure them. Fs is not the PCA internal PWM rate or ADC sample rate.
- STOP/safe-off/expired host session: cancel and assert reset, then release I2C.
- I2C error, bad readback/health or missed phase: request reset and latch fault.
  Do not bypass the error to get a light pulse.
- No ADC, intensity, HbO/HbR or FRAME packets. SIMULATED remains set even though
  LEDs are real in this profile; check info capability led=REAL_BRINGUP.
- Fuel-gauge reads are deferred for the entire active LED bus window. Cached
  battery age can exceed 300 s on a long scan; it must not be presented as fresh.

## Build only, before board work

Use the existing ESP-IDF 6.1 terminal from the repository root. This isolates both
build and sdkconfig from the already-tested communication image:

```powershell
idf.py -B build/led-bringup -D SDKCONFIG=build/led-bringup/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup' -D IDF_TARGET=esp32s3 build 2>&1 | Tee-Object led-build.log
# Only after a successful build:
idf.py -B build/led-bringup size 2>&1 | Tee-Object led-size.log
```

Record build/version/compiler/warnings/size and generated configuration. Confirm
CONFIG_NN2_LED_BRINGUP, GPIO_CTRL_FUNC_IN_IRAM, GPTIMER_ISR_CACHE_SAFE and
GPTIMER_CTRL_FUNC_IN_IRAM are enabled, PSRAM disabled and main stack >=12288.
Inspect bootloader link map for nn2_reset_led_hook and bootloader_before_init;
syntax alone cannot prove linkage. Preserve image/source/config hashes.

To retain the old simulated drive behavior, keep CONFIG_NN2_LED_BRINGUP unset.
The standard communication defaults do not enable it. Generated sdkconfig values
override defaults: use the same -B/-D settings for menuconfig/reconfiguration.
Do not flash the new candidate solely because it compiles.

## Physical test procedure for review and Owner authorization

Status: draft procedure, not yet approved. Hardware/RTOS and independent reviews
remain pending under AGENTS.md. No physical test/flash has been run by the agent.

Prerequisites: confirm assembled board/module/cable mapping; REXT=1 kOhm;
GPIO18 reset and external pullups; controlled supply and compatible battery
charging procedure (charger auto-enables at boot); thermally suitable fixture;
no human contact/exposure; appropriate NIR optical containment/measurement.
CATH_735/CATH_850 are sink connections, NOT board ground. Define the physical
energy-disconnection method and stop operator before any energized run.
Record supply limit, acceptable LED/current/temperature limits and instruments
with the responsible Hardware reviewer. No numeric thermal/optical acceptance
limit is invented by this firmware.

After the specific flash/bench action and the procedure are approved:

1. Keep emitter energy isolated while flashing/booting. Capture reset voltage
   through power-on/ROM/bootloader/app handoff; no unexpected pulse is acceptable.
   The hook cannot establish off through total CPU/interrupt failure.
2. Check boot logs for no startup failure, then info and get-config: real LED
   capability, Fs5, 20 currents30, STOPPED. Measure off behavior before restoring
   emitter energy under the reviewed fixture procedure.
3. Begin a short scan only in that fixture, using the explicit commands below.
   Capture actual current and phase edges. Verify one channel at a time, correct
   source/wavelength mapping, no activity on unused channels and no deadline miss.
4. Verify STOP/safe-off, lost heartbeat and disconnect cause off; record physical
   traces, not only application state. Do not increase Fs until initial traces
   and temperature/current behavior meet the preselected limits.
5. For the strict profile only, if separately authorized after the first stage passes, repeat at 10 Hz and compare phase/commit latency.
   Test all configured wavelengths/channels using instruments; a visual check is
   insufficient for NIR. This does not qualify the future ADC settling/K windows.
6. End with STOPPED, RESET held, led_bus_free=1 and cached telemetry able to
   refresh again. Isolate emitter energy before changing wiring or recovering.

Stop immediately on unexpected light/current, temperature outside reviewed
limits, fault/deadline miss, reset/watchdog or an incorrect channel. Disconnect
energy per the fixture procedure, retain the first fault/log/traces and review
before resuming. No human optical tests, protection bypasses, eFuses or security
changes are part of this work.

## Strict-profile commands once the physical entry gate is satisfied

```powershell
python tools/nn2ctl.py info
python tools/nn2ctl.py get-config
python tools/nn2ctl.py run --led --seconds 2
python tools/nn2ctl.py status
python tools/nn2ctl.py usb-status --serial-port COM11
# Later, after initial timing/current checks pass:
python tools/nn2ctl.py run --led --rate 10 --seconds 2
# Restore the selected default configuration while STOPPED:
python tools/nn2ctl.py set-fs --rate 5
```

The first run leaves the current Fs unchanged (boot5); --rate configures before
START. run holds one endpoint, prints start/heartbeat/stop and waits for stop
completion. A separate process cannot control that peer's session except public
safe-off. `python tools/nn2ctl.py safe-off` latches SAFE_OFF; clear-fault can return
PRECONDITION_FAILED until cleanup finishes. A fault may need physical isolation
and diagnosis before a clear/restart is authorized. CLI --led is explicit intent,
not authentication or a physical safety barrier.

USB STATUS adds led_state (0 IDLE,1 STARTING,2 RUNNING,3 STOPPING,4 FAULT),
led_frames (completed LED scans, not data packets), led_edges, led_missed,
led_max_us (phase work duration, not total deadline-to-commit latency), led_error
(ESP error), led_stack, led_bus_free and led_mode2. Application state numbers are
different: STARTING2, STREAMING3, STOPPING4, STOPPED1, FAULTED6, SAFE_OFF7.
First alarm begins slot0 after one half-slot delay; subsequent frame periods are
absolute. These are software scheduling semantics, not measured optical times.

New diagnostics: led_eflags is12 hex digits in EFLAG0..5 order, containing the
last per-channel samples taken while that channel was ON. led_ef_valid is a
24-bit hex mask; only set bits are observed channels. led_open, led_short and
led_ef_invalid use the same hardware PCA channel bit positions. Zero-current and
unused channels are not marked sampled. Reset/cleanup retain cached evidence;
the next START clears it. These are samples from different slots, not an atomic
snapshot or proof of optical output. The CLI prints a PCA_EFLAG JSON line with
decoded channel lists when these fields are present.

## Report

Send LED build/size logs, sdkconfig, exact source/image/bootloader hashes, physical
board/module/cable identification, review/procedure authorization reference,
startup log, info/config output, run output and before/during/after USB STATUS.
Include measured current, supply, temperature, reset/phase traces, tested Fs,
channel mapping and first failure. Keep evidence classes separate. Roll back via
a matched previously reviewed firmware/client and bootloader recovery procedure;
source changes alone do not replace a flashed bootloader.

## Optional SHORT advisory software candidate

Append `sdkconfig.led-short-bench` after the LED overlay in a NEW build directory
with its own SDKCONFIG. This does not select the profile in an existing config:

```powershell
idf.py -B build/short-advisory -D SDKCONFIG=build/short-advisory/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.communication;sdkconfig.led-bringup;sdkconfig.led-short-bench' -D IDF_TARGET=esp32s3 build
idf.py -B build/short-advisory size
```

INFO must show `short_policy=BENCH_WARN;limit_s=2;single_run=1`. Strict INFO shows
`short_policy=STRICT`. Advisory START requires Fs5 and all twenty nominal30mA
setpoints; changed/zero currents and Fs10 are rejected in firmware. A single
accepted START consumes the boot attempt even if cancelled during prepare or
followed by fault-clear. STOP, host expiry and safe-off still cancel. Heartbeats
cannot extend the absolute two-second timer ceiling. At expiry reset is asserted
and fault latched. No automatic retry or restart is available.

The CLI additionally requires `--led --ack-short-warning` and rejects advisory
`run` durations over2s. These switches are protocol guards, not authorization to
operate hardware. No live command or physical procedure is authorized here.

New on-demand USB fields: `source` (configured source SHA), `image` (ELF identity), `short_policy`, `limit_s`,
`single_run`, `led_attempt`, `led_cap`, `led_stop`, `led_epoch`, `led_complete`,
`led_fault_epoch`, `led_warn`, `led_dispatch_us`, `led_completion_us`. `led_stop`
codes:0 none,1 cancel (STOP/safe-off/lease expiry),2 cap,3 deadline,4 timer,
5 I/O/readback/diagnostic,6 cleanup. The app fault/lease counters distinguish
host causes. First cause/error and cached diagnostics survive clear-fault.
Frame epoch is run-local; boot identity plus accepted attempt identify advisory
runs. `led_max_us` includes failed operations; dispatch/completion maxima are
relative to the scheduled phase. Unchanged accepted SHORT adds only a cached
warning-frame count; it emits no unsolicited messages. Raw EFLAG/masks are merged
per-slot samples, never an atomic snapshot or proof that a real short is absent.

NXP detection eligibility is explicit: PWM255 is written/read back; full ON/OFF
images, unused-channel zero and100kHz bus remain. Sampling is at the existing ON
interval's end. Physical settling and the200ms MODE2 observation interval need
specialist acceptance. Before flash/energization, Nori must obtain numeric limits,
complete energy isolation, an isolated nonhuman fixture/procedure, matched source/
image/config and recovery image, specialist reviews and specific Owner confirmation.
The2s ceiling is not a qualified thermal/optical exposure limit.
