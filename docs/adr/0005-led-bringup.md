# ADR-0005: Opt-in LED-only bring-up and 5 Hz default

> 2026-10-27 follow-up: Owner confirmed V2/V3 as tested reference baselines and
> excludes V4 from the current investigation. Normal OFF now uses the full V3
> seven-byte image; ON adds one active-channel EFLAG read and cached diagnostics.
> Current/Fs/fault limits are unchanged. Earlier V4 comparison rows below are a
> historical study record, not validation or the basis for this corrective work.
> See ../PCA9956B_FAULT_267_INVESTIGATION.md and its evidence record.

Date: 2026-10-27. Status: local implementation candidate for specialist and
independent review. Not a flash/energization authorization or timing acceptance.

## Owner scope and acceptance

The Owner requested study of GitHub PRs and historical V3/V4, implementation in
the current firmware focused on LED drive, maximum Fs=10 Hz and default Fs=5 Hz.
In the follow-up the Owner selected all optical channels with default 30 mA.
Use the accepted ADR-0002 set {1,2,4,5,8,10}, 20 slots and nominal 50/50 phase
split. All channels means the 20 connected optical sinks, sequentially one at a
time, never all 24 driver outputs simultaneously. Keep emitters off at boot;
only explicit START in the opt-in profile initiates a scan.

Scope: current application/control binding, a portable optical-control port,
register adapter, LED-only task/timer realization, early reset hook, safe-off
readback preflight and host tests. Excluded: ADC, intensity/HbO/HbR processing,
FRAME producer, human exposure, hardware qualification, flash, release and merge.
Accountable activity: local firmware implementation. Required reviews: Hardware
(pins, Rext, reset, optical/electrical limits, cable), RTOS (ISR, task, IRAM,
locking, deadline, stack/heap) and independent review by a nonauthor. These are
pending; the implementation is not self-approved.

Acceptance: boot cannot select an ON output; register readback precedes nonzero
drive; each ON burst contains at most one enabled sink; unused outputs have zero
IREF; all allowed frame periods are exact; configuration is immutable while
starting/running/stopping; cancellation cannot be undone by a pending start;
late/failed transactions request RESET# low and latch a fault; the bus is never
simultaneously owned by LED and battery clients; real-LED CLI starts are explicit.

## Sources and differences

Read GitHub on this date through the connected Composio GitHub account, without
writes. Relevant upstream PRs:

- [#22](https://github.com/YerriSuyudHasim/noctinirs-2/pull/22) head
  526a7ee9bbce6e1d9b08907538fc12f126e5ec85 and
  [#24](https://github.com/YerriSuyudHasim/noctinirs-2/pull/24) head
  0375d0c9a0e1b76956fe97f8fe51ef4c2d46b016: accepted timing design, not a driver.
- [#20](https://github.com/YerriSuyudHasim/noctinirs-2/pull/20) head
  864047d38d01b8e7ee81d665f3a6f45046e5ba7e: hardware input/reviews; still Draft.
  [Hardware review](https://github.com/YerriSuyudHasim/noctinirs-2/pull/20#issuecomment-6208941908)
  compared the local LED datasheet (100 mA/die absolute maximum) but leaves
  thermal/optical/cable qualification open. This does not qualify 30 mA exposure.
- [#60](https://github.com/YerriSuyudHasim/noctinirs-2/pull/60) head
  d3478d6d0747908460701e68f766247c80dbc2e0: early reset and safe-off design.
  Adapt its GPIO hook and zero/readback/reset sequence locally; do not cherry-pick
  unresolved HW-TBD defines or claim the whole PR is merged/implemented.
- [#55](https://github.com/YerriSuyudHasim/noctinirs-2/pull/55) head
  222f9c45f82b6014db85583924da00e313e5499c: current proposed control contract.

| Historical evidence | V3 | V4 | Current bring-up |
|---|---|---|---|
| Driver state | Async callback, completion ring, separate recovery task | Sync verified golden path plus async owner and scheduled completions | Sync verified initialization, one bounded LED-only owner; no completion ring or recovery retry |
| Commits | Prebuilt 7-byte one-hot LEDOUT bursts | Full ON burst, 2-byte normal OFF, full safety OFF | Same scoped intent, newly implemented; STOP-commit/OCH=0; normal OFF clears the sole active byte |
| Timing | Fixed 10 Hz geometry | Fixed 10 Hz with optional OFF offsets and runtime 800 kHz qualification | Exact configurable Fs, default 5; 50/50; 100 kHz selected for first bring-up, not 800 kHz lifted from history |
| Failure | Completion/recovery counters | Readback, EFLAG diagnostics, epochs and latency stats | No catch-up burst; reset on I2C/health/deadline failure; explicit recovery; generation cancellation and USB diagnostics |

Read-only paths: reference/v3/components/nocti_v3_led, its config/board;
reference/v4/components/nocti_v4_led, nocti_v4_async_led, nocti_v4_i2c and scheduler
config. Snapshots are neither modified nor build inputs. No wholesale copy.

## Hardware binding and register decisions

Current source: Schematic_and_hardwareInfo/SCH_Thesis_main_2026-10-16.json and
BOM_Thesis_main_2026-10-16.csv, not historical constants. Main U8=PCA9956B;
GPIO18 RESET_LED; SDA15/SCL16 shared with power ICs; AD0/1/2 grounded gives
seven-bit address 0x01. Main:R4=1 kOhm (BOM RT0603BRD071KL) programs REXT.
Optical slot mapping uses channels 0..9,14..23; 10..13 remain zero/off.
The mapping is a schematic-derived binding, not a verified assembled cable map.
Assembled board revision and optical source wiring must be checked before use.

Primary register/timing reference: local Datasheet/PCA9956B.pdf Rev 1.2 and
[NXP datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9956B.pdf), sections
7.1,7.2,7.3,7.6. MODE1=0x40 disables group/subcall and selects config auto-increment;
MODE2 writable bits=0 selects change on STOP. Safe-off first writes IREFALL=0,
PWMALL=0 and six LEDOUT registers=0 with readback. SLEEP is never the off mechanism.
Nonzero per-channel IREF is programmed/read back only with verified LEDOUT zeros.
30 mA at 1000 ohm becomes floor(30*4000/900)=133, nominal 29.925 mA. Hardware
tolerance applies. Zero current disables that slot. Bring-up enforces a tighter
0..30 mA limit even though the control simulation/Owner global cap allows 50 mA.

P20-H2 remains: OE# grounded and RESET# pulled up, no hardware energy cutoff.
The bootloader hook requests GPIO18 low earlier but does not cover ROM before
the hook, a frozen CPU or all failures. Neither a log nor success return proves
physical off. CATH_735/CATH_850 are constant-current sink wires, NOT ground;
grounding them bypasses driver current regulation.

## Layering and diagnostic scheduling

Core defines only frame-rate validation and half-slot geometry; the new port
contains no framework/device addresses. Application requests start and observes
STARTING -> STREAMING, STOPPING -> STOPPED via the port. Platform code owns
registers/GPIO/I2C/task/GPTimer. Simulation without the port remains supported.
Default config is now 5 Hz and 30 mA on 20 slots in both modes, reset on reboot;
it is not persisted. Querying/get-config never energizes LEDs.

Opt-in CONFIG_NN2_LED_BRINGUP defaults off. It adds one nn2_led task, provisionally
Core1/priority5/6144-byte stack. This combines timer wake consumption and LED
submission for LED-only diagnostics, not the full ADR-0003 LED>scheduler>ADC
topology. No ADC task exists here. Do not reuse these provisional numeric values
as the acquisition priority ladder. The ADR-0003 decomposition is required when
ADC/real frame acquisition is introduced. This diagnostic boundary change needs
the pending RTOS/independent review.

Choose synchronous ESP-IDF 6.1 master I2C, queue depth zero, 100 kHz, 20 ms transfer
timeout, one exclusive owner. The installed 6.1 API remains current; synchronous
mode is a deliberate bring-up choice, not an obsolete IDF5 API. Async callbacks
remain experimental and bus-wide. Their lifetime/drain complexity is not useful
for initial LED-only diagnosis. This is not an accepted acquisition commit budget
or a claim that the provisional 250 us budget is satisfied. At 100 kHz a full
ON burst alone has roughly 720 us of wire bits before software overhead.

GPTimer runs at 1 MHz with absolute one-shot deadlines, 40 phases/frame. ISR
sets the next absolute alarm, checks an unfinished/late preceding phase and wakes
the owner. No I2C, logging or allocation in ISR. A missed phase requests GPIO18
low and latches failure. GPIO/timer control and callback must be IRAM/cache-safe;
Kconfig selects and compile guards enforce the corresponding options. No custom
watchdog feed/disable. A deadman based on this ISR cannot protect against total
interrupt/CPU failure; physical energy isolation is still necessary for first tests.

Stop/safe-off cancels pending work and asserts reset synchronously under the same
short spinlock as reset release. Worker completes/drains, stops/disables timer,
removes devices and deletes the bus. Cleanup failure retains handles and faults;
clear-fault refuses reuse before quiescence. Exact STOP acknowledgements survive
asynchronous teardown; old-session cached replies cannot refresh a new session.

Battery deadline policy stays idle60s/acquiring300s. During a real LED scan there
is no battery bus window; due reads are deferred until cleanup releases I2C0.
This is an explicit first-bring-up limitation: a continuous >5-minute scan does
not promise a fresh 5-minute fuel-gauge sample. Do not use a second controller
on the same wires or reset/delete the LED bus to force a read.

## Control and evidence limits

The envelope/registry stays ADR-0004/1. Info adds led=REAL_BRINGUP when bound;
START/STOP can return STARTING/STOPPING, which are acceptance acknowledgements,
not hardware completion. Cached ACKs retain their original state/timestamp;
query current state separately. USB STATUS adds LED state, completed LED scan
frames/edges, deadline misses, work duration maximum, error, stack and bus status.
These scan frames are counters, not emitted FRAME packets. No DRDY timestamp,
ADC sample, intensity or concentration is produced. SIMULATED remains set for
this incomplete acquisition binding; it MUST NOT be read as LED power disabled.
The Python CLI requires --led for START/run against real LED capability and
rejects --led against simulation, making a changed profile visible to the operator.

Verification, exact source hashes, limitations and rollback are recorded in
docs/LED_BRINGUP_EVIDENCE.md. Target build, linked-hook inspection, binary size,
stack/heap measurement, logic-analyser traces, emitter/current/thermal/optical
qualification and independent review are required before acceptance. No target
build or physical energization by the agent is implied by host tests.

Rollback: preserve source/config/image identities and restore a reviewed matched
firmware/client pair. Build communication with CONFIG_NN2_LED_BRINGUP unset to
restore simulated START. Bootloader hook changes require matching bootloader
inspection and recovery planning; source reversion alone does not replace a
flashed bootloader. No partition/eFuse/signing/encryption setting is changed.
