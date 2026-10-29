# Feat35 LED integration candidate

Local candidate,2026-10-29 KST. No build,commit,push,merge or hardware action.

## Baseline and scope

The current working tree imports checkpoint167d18b6a602dee06331b440a489aa3b063a5dc3
from feat/35-ads131-standalone without moving/merging either branch. The standalone
branch remains the recorded ADC-only checkpoint. All scoped transport,queue,ISR,
qualification,diagnostic and deferred-LED-start changes are carried forward.

The acquisition overlay and local generated configuration now disable
NN2_ADC_V4_DARK_BASELINE. LED hardware preparation runs while ADC is stopped,
verifies outputs OFF and allocates/configures I2C/timer resources. ADC initializes
and qualifies1024 reads; then LED timer activation is queued under the existing
permission/generation interlock. Producer/consumer sample transport remains
separate from control requests, with256 samples and fatal overflow. Continuity
uncertainty remains visible and invalidates optical pairing; CRC is never bypassed.

## Adjustable Fs

No rate is hardcoded for a run. The existing acquisition contract accepts
exactly2,4,5,8,10Hz; the minimum remains2Hz. SET_FS is accepted only when STOPPED.
Runtime retiming and additional rates are not introduced by this integration.
ADC stays8000SPS at every optical scan rate. LED runtime I2C stays800kHz and
configuration/power transactions100kHz; CPU240MHz and16MiB flash are unchanged.
The earlier conversational suggestion of changing Fs during streaming was wrong;
source/state-machine rules are preserved here.

## Owner build and first integration test

After the Owner builds/flashes this candidate, INFO must show
led=REAL_BRINGUP;frames=ACQ1 and must not show baseline=V4_DARK.
Under the existing separately authorized LED bench procedure, first run:

```powershell
python tools/nn2ctl.py run --rate 5 --seconds 10 --led --acquisition --ack-short-warning
```

Collect INFO,terminal results,STATUS,ads131_rate,ads131_drain and latency/unknown
logs after STOP/fault. Acceptance requires clean ADC counters,zero LED timing/I2C
errors and real valid optical frames,not merely a STREAMING state. ADC-only bench
results are a comparison baseline,not evidence of LED integration success.

After this result is assessed,repeat at other supported rates by changing --rate
between sessions. Explicit separate commands are also available while STOPPED:

```powershell
python tools/nn2ctl.py set-fs --rate 2
python tools/nn2ctl.py set-fs --rate 10
```

Do not use these commands to retime an active session. Long-duration and
multi-rate integration acceptance remain pending; no new board operation is
performed or inferred by this local edit. Hardware/RTOS and independent review
remain required. Rollback is restoring the standalone checkpoint/configuration;
never erase device settings as an automatic recovery step.

## Verification limits

Only source/config review,diff whitespace checking and historical-file integrity
are performed locally. Host regression sources and firmware builds are not run
per Owner instruction. The32-bit/36KiB static-memory ceiling,IRAM placement and
new integration path still require the Owner's build/map/board validation.
See FEAT35_V4_BASELINE.md for the baseline comparison and bounded diagnostics;
its default-dark build instructions apply to the standalone checkpoint,whereas
this branch's current integration overlay intentionally defaults to LED mode.

## Boot66 diagnostic follow-up: correlate DRDY and LED interlocks

The Owner reported warmup frame0 and valid paired frames1/2 before fault267.
The first uncertainty was a195us ISR-timestamp interval; adjacent transports
started21/25us after ISR entry and lasted33/37us, with status010f and no CRC,
overflow or counted loss. Pairing rejects GAP quality and requests emitter OFF;
this patch preserves that behavior. No additional ADC run time is introduced to
collect diagnostics and no acceptance/CRC/Fs setting is relaxed.

After STOP/fault, ads131_followup looks up the next4 exact serials in the existing
128-slot event ring. Logical queue clearing retains slot contents. available=0
means the event was never captured or its slot was overwritten; it is not
proof that the physical conversion was missing. interval_known=0 forbids inferring
an adjacent period from a missing predecessor. Printing is deferred and lookup
is protected by the existing edge lock; no new sample or ISR-history buffer is
allocated. The lookup does not extend capture after a stop request.

On ESP acquisition builds, the LED module now has a bounded <=2KiB recorder:
64 recent records,at most16 printed after successful ADC STOP acknowledgment,
and the longest recorded LED interlock hold. kind1 is the software-owned s_lock
hold (line identifies its entry in nn2_led_bringup.c); only holds>=8us are added,
except the partial hold recorded at freeze. kind2 is the complete PCA phase
operation including optional end-of-frame health reads. Each record has begin/
end software timestamps,phase and frame. Stop/cancel freezes records before
cleanup. Acquisition composition prints led_timing_summary/led_timing only after
ADC STOP ACK. No LED timing logs are emitted while acquisition is active.

Instrumentation covers this module's task/ISR interlocks,not interrupt masking
inside ESP-IDF or other modules. Hold timing excludes waiting to acquire the
lock and excludes the final record-write/unlock overhead; it is not a measurement
of all interrupt-disabled time. Extra timer calls/stores perturb timing and need
bench assessment. IRAM helpers use scalar stores/internal state and the existing
IRAM timer configuration. Host LED seams retain direct lock calls; hardware-path
IRAM/map and code-generation checks remain for the Owner build.

The record storage and overhead are diagnostic changes,not a fix for delayed
interrupts. Capture cannot prove physical DRDY timing; if correlation remains
ambiguous,the next step is separately authorized DRDY/CS/SCLK logic-analyzer
measurement. No pin allocation,filter,clock or emitter-current change is made.

The test command stays unchanged:

```powershell
python tools/nn2ctl.py run --rate 5 --seconds 10 --led --acquisition --ack-short-warning
```

Collect all ads131_unknown/followup/latency/rate/drain,led_timing logs,terminal
frames and STATUS. The new host regression source checks exact lookup after queue
clear and rejection after overwrite; it has not been compiled/executed. The LED
instrumentation itself is source-reviewed only. No build,commit,push,flash or
hardware operation is performed. Historical integrity and diff checks are the
only executed checks for this diagnostic edit.


## ADR-0003 task ordering and reduced timing instrumentation

Local candidate after user-reported boot 69; no build or target validation run
by the author. LED remains Core 1 / priority 22; ADS moves from priority 23 to
20 on Core 1, restoring the accepted LED-above-ADS ordering. The pairing/raw
consumer moves to Core 0 / priority 12 through the shared RTOS configuration.
There is no separate scheduler task in this bring-up adapter: GPTimer and the
LED owner implement phase dispatch. This change does not claim full ADR task
inventory compliance or qualified latency/stack margins. Core 0 Wi-Fi contention
and increased ADC preemption by LED remain bench risks.

Intrusive LED lock/phase timing is disabled at compile time, including its
recorder storage. Production ADC split latency capture is disabled: extra ISR
publication, owner-resume, service-entry, driver-call and CRC timing reads are
removed, as are detailed latency logs. Existing compact trace storage remains
allocated for first-failure/unknown evidence; no ADC RAM reduction is claimed.
Host fixtures retain detailed capture. CRC validation, freshness and period
quality checks, first startup/runtime failure, first unknown with follow-up
intervals, rate summary and loss/drain counters remain enabled. No acceptance
threshold, SPI setting, optical Fs selection, LED bus speed or STOP protection
is relaxed. Startup identifies `timing=essential`.

Use the same previously authorized bench procedure and command. Inspect
`ads131_unknown`, startup/runtime faults, `ads131_rate`, `ads131_drain` and
STATUS; `led_timing*` and `ads131_latency*` are intentionally absent. Compare
per-run counter deltas. Changes combine task placement and instrumentation
reduction at the owner's request; improvement cannot identify which contributed.
Rollback by restoring ADC priority 23 / consumer Core 1 and enabling detailed
timing in a separately reviewed candidate. No commit, build, flash or push.


## V4-7.2 scheduler/LED handoff adaptation (supersedes combined owner)

Owner requested a V4-derived IDF 6.1 baseline before further development.
This is an unbuilt local implementation candidate, not successful target evidence.
The preceding boot-69 instrumentation and intermediate combined-owner topology
sections describe older candidates; the following is the current implementation.

### Source comparison and concrete differences

Read-only sources studied: `reference/v4/components/nocti_v4_scheduler/`
(deadline handler, next absolute deadline, submit-on/off, completion processing),
`reference/v4/components/nocti_v4_async_led/` (persistent payloads, submit/wait,
scheduled completion identity), and `reference/v4/components/nocti_v4_ads/`
(DRDY event handling, post-SYNC discard and bounded backlog). V4 defines LED
22, scheduler 21 and ADS 20 on Core 1. Its LED task submits one prepared payload,
blocks for completion and publishes a scheduled completion. The scheduler does
not synchronously wait for the LED command. Payloads are ON seven bytes and
selected-register OFF two bytes. Existing NN2 PCA preparation already built
these persistent payloads; no per-edge payload rebuild was found.

The old NN2 combined owner called `uxTaskGetStackHighWaterMark(NULL)` after
every wake/edge, at priority 22. Historical V4 exposes separate explicit stack
sampling functions. The scan is now performed during preparation, before
`prepared=true` permits composition to start ADC acquisition. This removes a
concrete unnecessary high-priority workload; it does not prove that scan alone
caused the bench fault. Local IDF 6.1 source
`C:/esp/v6.1/esp-idf/components/esp_driver_i2c/i2c_master.c`,
`s_i2c_transaction_start`, also contains bus timing and FIFO configuration
inside an interrupt-masking spinlock. Its duration has not been measured and
this patch neither modifies nor replaces the SDK driver.

### Current task and ownership pipeline

| Execution context | Core / priority | Work |
|---|---|---|
| GPTimer ISR | Core 1, allocated by LED preparation | Append generation/id/phase/frame/deadline to static eight-entry ring; rearm from previous absolute alarm |
| `nn2_sched` | Core 1 / 21, 6144-byte requested stack | Dispatch without waiting; accept matching completions; publish acquisition windows |
| `nn2_led` | Core 1 / 22 | Own PCA/I2C buffers and resource preparation/cleanup; submit prepared ON/OFF; block for ISR completion; publish completion |
| ADS owner | Core 1 / 20 | Existing V4-derived polling SPI and bounded DRDY handling |
| Pairing consumer | Core 0 / 12 | Drain ADC, accept windows, build ACQ1 frames |

`main/adapters/nn2_led_scheduler.inc` is a separate adapter-private scheduler
module included by the LED adapter so deadline handoff and STOP/reset retain a
single interlock. No public Port or Core dependency on FreeRTOS was added; a
separate IDF component is unnecessary for this internal hardware-owner boundary.
Timer handles are prepared and cleaned up by the LED resource owner, while
runtime phase dispatch/window publication belong to the scheduler task.
Static completion capacity is four. Handoff record storage has a 1 KiB ceiling;
actual linked RAM/IRAM/stack usage remains unmeasured. No per-edge allocations,
stack scans or log formatting were added. Queue capacity and deadline failures
hold emitter reset. Completion generation/id/phase/frame must match the in-flight
command. STOP discards overtaken completions and release acknowledgement waits
for scheduler publication to quiesce before another session can start.

I2C completion ISR time now anchors the optical commit, rather than the time
after PCA frame-health reads/window processing. ADC first-unknown `checked_us`
again records time at the anomaly, without restoring per-sample CRC timing.
Production split timing remains disabled. Boot prints the selected task mapping;
INFO identifies `baseline=V4_PIPELINE`.

### Deliberate retained NN2 boundaries, not identical historical firmware

- Active board pins, current limits, 16 MiB flash, 240 MHz CPU, ADC clock/config,
  runtime I2C 800 kHz and power/config I2C 100 kHz remain the authorized settings.
- Fs stays STOPPED-configurable among 2/4/5/8/10 Hz. Equal ON/OFF uses the V4
  qualification-style 50/50 geometry scaled with Fs, rather than copying V4's
  fixed 10 Hz / default 2200 us precompensated OFF setting.
- PCA EFLAG sampling before OFF and frame-health readback remain enforced.
  Therefore OFF includes a read then the two-byte OFF write; frame-end also
  checks health. These extra checks were not removed to imitate V4's minimal
  one-write edge command. Their blocking waits remain in the I2C owner.
- Existing freshness/period GAP rejection, CRC, FIFO/backlog bounds, STOP,
  timeout and poisoned-driver recovery rules remain. No claim is made that V4
  would reject exactly the same quality record. ACQ1/host protocol remain NN2.

### Validation, risk and next bench evidence

Author static checks: `git diff --check`, INFO string capacity <=196 bytes,
and read-only reference integrity (319 files). Host source regressions updated
for ISR -> scheduler -> LED -> scheduler handoff, overtaken completion on STOP,
mismatched command identity, and write-completion window timestamps. They were
NOT compiled or executed. No firmware build/link, flash or hardware operation.
Target is ESP32-S3 / installed SDK 6.1; toolchain, warnings, linked size and
runtime margins must be recorded from the owner's build. No specialist or
independent review is claimed; required before merge/qualification.

Previously authorized bench procedure/command remains unchanged. First inspect
boot mapping `led=22/core1 sched=21/core1 adc=20/core1 consumer=12/core0` and INFO
`baseline=V4_PIPELINE`. Capture STATUS, terminal results and ADC summaries after
the run. Begin with the existing short run before extending duration/rates.
Success requires observed run completion and zero per-run loss/overflow/unknown,
not merely a successful build. Additional scheduler task stack and Core 0 Wi-Fi
contention need measurement. IDF internal critical sections can still delay ADC
ISR entry; removing stack scans and splitting tasks are not a timing guarantee.
Rollback by restoring the local candidate's scheduler/LED/config/test edits as
a group, preserving all earlier uncommitted ADC/board work and the saved
standalone branch; do not hard-reset the shared working tree.
