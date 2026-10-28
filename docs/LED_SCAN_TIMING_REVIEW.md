# Review: Fs8..10 intermittent LED scan faults

Historical analysis of the Owner's synchronous100kHz image. The subsequent
Owner-authorized implementation is documented in [LED_ASYNC_RUNTIME.md](LED_ASYNC_RUNTIME.md).
The calculations below describe the failing baseline, not the new runtime.

Acting as Agent03 Firmware Developer, Codex/OpenAI. Scope: review the Owner's
boot16..20 logs against current source and installed ESP-IDF6.1 configuration.
Base HEAD23eb04217463772887cd5b755d5a0b3920a679a5 plus existing uncommitted work.
No firmware/configuration behavior was changed by this review. No firmware build,
link, size/map, flash or device access was run. Source accepts2..10Hz, but prior
host model/syntax checks do not establish physical worst-case timing at10Hz.

## What the supplied log establishes

| Fs | Supplied scan attempts | Outcome |
| --- | --- | --- |
|2 |5s on boot20 | STOPPED, fault0 |
|5 | Two5s runs on boot17 | Both STOPPED, fault0, same-boot restart works |
|7 |60s on boot20 | STOPPED, fault0 |
|8 |2s and5s, then60s on boot19 | First two pass; third faults, observed about1.030s after START reply |
|9 |2s on boot18 | Fault267 observed about1.030s after START reply |
|10 |10s on boot16,2s on boot17,5s on boot20 | All fault267, observed about0.520..1.030s after START reply |

The second Fs10 command on boot16 is rejected while already FAULTED, before a
new START; it is not a second independent scan failure. Durations are client
requests, not measured physical emission times. The Fs8 request for60s fails
near the first second, not after60s of heating. This does not exclude heating
from earlier runs or another hardware/diagnostic fault. User-reported visual
scan counts and functional successes are not waveform/electrical qualification.

INFO consistently reports BENCH_WARN, duration=HOST, limit_s=0, single_run=0.
There is no evidence of the previous STRICT/profile or2s-cap problem recurring.
Repeated5Hz runs demonstrate normal STOP cleanup/restart without a boot quota.
The highest rate demonstrated for60s in this supplied log is7Hz; that is observed
functionality, not a new imposed software ceiling or an established hardware limit.

All scan failures shown have application fault267 (0x010B), the generic optical
adapter wrapper in components/nn2_app/src/nn2_app_comm.c. It can represent deadline,
I2C/readback, MODE/EFLAG, timer or cleanup failure. The command error INVALID_STATE
is the result of already being faulted, not a specific root cause.

The USB STATUS snapshots in the pasted log are after board reset:
boot16 fault -> STATUS boot17; boot17 fault -> STATUS boot18; boot18 fault -> STATUS
boot19. They correctly show a clean new boot with zero counters. RAM-cached first
fault evidence survives a PCA reset/STOP, but not an ESP32 MCU reboot. Therefore
led_missed=0/led_error=0 in those snapshots cannot classify the previous failure.
The raw source/image labels are accepted; no redundant build/identity check is
requested. If boot20 remains faulted, only its cached STATUS is missing; no new
RUN is requested to obtain it.

## Timing calculation from the actual implementation

Relevant source locations:
- main/adapters/nn2_led_bringup.c: open_bus selects100,000Hz; alarm_callback faults
  busy/late work; phase_work calls health after the final OFF, phase39.
- components/nn2_core/include/nn2_core_optical.h:20 slots,40 half-slot phases;
  phase duration=floor(1,000,000/(40*Fs))us.
- components/nn2_board/nn2_board_pca.c: full7-byte ON/OFF bursts, one EFLAG read
  before OFF, then separate MODE1 and MODE2 reads at frame end.

At100kHz, counting9 SCL clocks per address/data byte:

| Operation | Clocks | Ideal wire time |
| --- | --- | --- |
| Full ON or OFF: address +7 bytes |72 |720us |
| One-register read: address-W, register, address-R, data |36 |360us |
| Ordinary OFF: EFLAG read + full OFF |108 |1080us |
| Last OFF: EFLAG + OFF + MODE1 + MODE2 |180 |1800us |

These are lower bounds excluding START/STOP/repeated-START setup, actual SCL
variation/stretching and all driver, ISR, task wake-up and critical-section costs.
The last OFF contains FOUR synchronous driver calls. Entire-frame traffic is
20*720 +20*1080 +720 =36,720us ideal wire time,62 driver transactions per frame.
At10Hz this is36.72% average bus occupancy, but the peak phase has much less room.
An average-bandwidth calculation alone is not a deadline guarantee.

| Fs | One phase budget | Margin after last-OFF wire time | Margin vs prior boot13 measured completion2649us |
| --- | --- | --- | --- |
|5 |5000us |3200us |2351us |
|7 |3571us |1771us |922us |
|8 |3125us |1325us |476us |
|9 |2777us |977us |128us |
|10 |2500us |700us |-149us |

The2649us observation was Owner boot13 at Fs5 (led_max_us2599, dispatch max72);
it is evidence that such a completion has occurred, not the new image's WCET
and not proof that every failure is the last OFF. It already exceeds a10Hz phase.
Small changes in ISR/driver scheduling can cross the tight8/9Hz margins, which
fits2s/5s passes and an early failure on another8Hz attempt. Once a phase misses,
firmware deliberately asserts reset and latches the optical fault.

Strongest hypothesis: inadequate worst-case phase margin in the current
synchronous100kHz implementation, especially the diagnostic-heavy final OFF.
Confidence: strong supporting source/arithmetic/previous timing evidence, but
UNCONFIRMED for these particular faults until retained led_stop/led_error/missed
and MODE/EFLAG evidence is available. Do not label them all deadlines from267 alone.

## RTOS, CPU and hardware assessment

Current local config: CPU160MHz, debug optimization, FreeRTOS100Hz tick, LED task
core1/priority5, GPTimer1MHz resolution with IRAM/cache-safe handling, I2C master
ISR handler in IRAM but CONFIG_I2C_ISR_IRAM_SAFE unset, PSRAM/PM disabled.
The source uses task notification from GPTimer ISR, not vTaskDelay for scan
phases. A100Hz OS tick does NOT limit the phase trigger to10ms. Raising the tick
rate alone would not remove the1.8ms wire cost or prove worst-case dispatch.

[ESP-IDF6.1 I2C documentation](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/peripherals/i2c.html)
documents synchronous API completion/blocking without registered async callbacks,
and default ISR deferral when cache is disabled. CONFIG_I2C_ISR_IRAM_SAFE is the
explicit cache-safe option; handler placement alone is not equivalent. Cache-off
or interrupt/scheduling latency is a candidate to examine, not a demonstrated
flash/cache event in the Owner log. CPU240MHz or release optimization may reduce
CPU overhead but cannot shorten SCL wire time. WiFi RTT is not local phase latency;
500ms heartbeats and the PC console do not trigger individual LED phases.

[NXP PCA9956B Rev1.2](https://www.nxp.com/docs/en/data-sheet/PCA9956B.pdf) specifies
an I2C interface up to1MHz. ESP32-S3 documentation specifies up to400kHz master
SCL. Thus100kHz is the current implementation setting, not a demonstrated ceiling
of either IC. This does NOT qualify the assembled board/pullups/shared-bus layout
for a higher clock. Bus is shared with the power ICs; no clock/current/pin change
is made or recommended as an unreviewed shortcut. Optical Fs10 is a100ms frame,
not a10Hz I2C clock. The IC bus-rate specification does not alone qualify pulse,
thermal, optical or full-system timing. No hard PCA or ESP32-S3 inability to scan
at10 frames/s is established by this evidence.

## Recommended correction sequence, without weakening checks

1. Use the existing cached STATUS of the faulting boot if still available. Map
   led_stop3 + led_missed to deadline; led_stop4 to timer; led_stop5 with error/
   MODE/EFLAG evidence to I2C/readback/diagnostic; led_stop6/bus_free0 to cleanup.
   ESP_ERR_TIMEOUT263 can come from different paths, so keep the first cause.
   MODE2 OVERTEMP or OPEN/invalid/mixed SHORT evidence must stay a real fault.
2. If timing is confirmed, reduce PEAK transactions before changing the hardware
   clock. MODE1/MODE2 are adjacent and supported in the existing auto-increment
   register range (NXP Table6): a reviewed two-byte health read would reduce
   two reads from72 to45 clocks, saving270us plus one driver wait, with all mode/
   thermal/ERROR tests preserved. EFLAG registers remain separate, outside that
   auto-increment range. Cached raw MODE2 handling/tests must change together.
3. Alternatively distribute mode readback across suitable OFF phases while keeping
   frame epochs and MODE2 observation guarantees explicit. Do not move OPEN checks
   after OFF, silently reduce sampling, reuse stale frame masks or weaken deadlines.
4. Review I2C cache-safe config/interrupt allocation and measured CPU/driver overhead.
   A driver2-byte read or scheduling candidate needs lightweight injected-failure/
   cancellation coverage and an Owner build; no speculative driver rewrite is
   performed before classifying the observed fault.
5. Consider a separately reviewed400kHz board/bus configuration only if still
   necessary; electrical compatibility and existing power-device ownership must
   be checked. No new physical measurements/disassembly are requested merely to
   complete this analysis. Actual improvement requires subsequent authorized
   timing evidence; no promise of stable10Hz is made from static arithmetic.

No hard ceiling below10Hz, new run duration limit, automatic retry/fault clear,
relaxed deadline or protection removal is introduced. Software can continue
being improved;7Hz is the best demonstrated60s operating point in this log.
The precise next action is fault-cause capture, then targeted peak-latency reduction.

Evidence summary and calculation inputs:
[led-scan-timing-review.json](evidence/led-scan-timing-review.json).
Class: source/static analysis plus Owner-reported outcomes. Firmware build/link/
size/map and physical operations by agent: NOT RUN. No compile/test result is
invented for this documentation-only review. Reference integrity is checked
before/after; previous uncommitted changes are preserved.
