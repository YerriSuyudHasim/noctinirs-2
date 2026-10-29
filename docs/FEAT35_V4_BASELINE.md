# Feat35: V4-derived ADC baseline and staged LED integration

Local implementation candidate, 2026-10-29 KST. No build, test compilation,
commit, push or hardware operation was performed for this revision.
This record supersedes earlier local single-event/250-us service-gap policies.
Historical V4 stays read-only; this is a scoped port through active interfaces.

## Transport, configuration and lifecycle comparison

| Behavior | Historical V4 | Active candidate |
| --- | --- | --- |
| SPI | mode1,8MHz,144 bits,guards1/1,duty128,noDMA,queue1 | Same settings; dedicated board SPI2 instead of V4 SPI3 |
| Transfer ownership | Polling, sole task, retained bus | Same ownership; split polling start/end retains finite completion wait and poisoned timeout storage |
| Reset / first frame | LOW300us,wait20us,NULL/FF24 | Same, plus CRC/padding checks on every command response |
| Configuration | UNLOCK,24-bit,CCITT,pulse DRDY,push-pull | MODE0111,write/readback; extra WREG acknowledgment check retained |
| Clock/gain/config/channels | 0F0A/0000/0600/0000 | Same; factory offset0/gain800000 read back without writing calibration |
| SYNC / settling | LOW5us,masked GPIO,3 discarded frames,1 expected F_RESYNC | Same settings; monotonic event serial with per-start baseline |
| Qualification | 1024 samples,mean8kSPS +/-5%,CRC/status/sequence checks | Same rate window; period outliers remain separately reported |
| Runtime event work | Pop oldest event,backlog after pop<2,sequence checked | Same acceptance,128 event slots; bounded owner batch before commands/watchdog |
| Runtime timing | Period statistics;5ms DRDY stall | Same watchdog basis; no production service-gap/age/end-serial blanket fault |
| Data delivery | 256 raw records,producer/consumer separated | 256-record static RTOS queue,blocking direct consumer read; no CMD_READ round trip per sample |
| Full raw buffer | Counted fatal overflow | Counted fatal overflow; never overwrite older records |
| Consumer priority | 12 | 12; ADC stays23 above this project's LED22 |
| Cache policy | IRAM GPIO ISR/internal state | IRAM scalar ISR,internal DRAM,timer/FreeRTOS/SPI IRAM configs |

Intentional integration constraints remain: active board GPIOs/clock contract,
240MHz CPU,16MiB flash,800kHz LED runtime,100kHz configuration/power transactions,
minimum optical Fs2Hz, public wire format, command deadlines and acknowledged
STOP/reset. The transport split preserves IDF6.1 timeout ownership semantics.
Firmware linking/map/physical timing have not been validated by this source edit.

The ADC domain/adapter keeps CRC and status rejection. For recorded production
events, runtime period over188us or incomplete channel freshness marks a sticky
GAP quality and increments unknown-loss indications. Dark baseline drains and
reports these records; optical pairing rejects any nonzero quality. Consequently,
a session that stays RUNNING with unknown_loss>0 is not a passing continuity test.
No bit shifting, CRC bypass, invented timestamp or silent catch-up is introduced.

## First test: ADC only, emitters held in reset

The versioned acquisition overlay and current local generated sdkconfig select
CONFIG_NN2_ADC_V4_DARK_BASELINE=y. This skips LED preparation and activation.
INFO must advertise led=OFF;baseline=V4_DARK;frames=unsupported. It produces no
optical measurement frames. The host run command monitors the real ADC session.

After the Owner builds/flashes the acquisition profile, use:

```powershell
python tools/nn2ctl.py run --rate 5 --seconds 10
```

Do not use --led/--acquisition for this baseline. Those flags deliberately reject
the dark/no-optical-frame capability. If INFO does not show baseline=V4_DARK,
the expected configuration/image is not active; retain INFO/source/image details.
No host-tool permission check is bypassed or changed.

Collect terminal output, STATUS and ads131_rate/ads131_drain after STOP. On fault,
collect ads131_runtime_detail,ads131_runtime and all8 ads131_trace records.
A successful first run requires valid qualification, no ADC fault/CRC/overflow/
unknown loss or read_late indications, and approximately8k delivered samples per
second of running time. Produced/consumed counts are boot totals; compare deltas
for repeated runs. STOP-tail disposal is reported separately as stop_discarded,
not mistaken for a runtime overflow. No optical validity is established by this.

## Second test: integration, only after the dark baseline is assessed

Disable CONFIG_NN2_ADC_V4_DARK_BASELINE in the selected generated configuration
(menuconfig). Its default remains y in the acquisition overlay for fresh local
builds. With the option off, INFO again advertises LED/ACQ1 capability.
The existing separately authorized LED procedure/command applies:

```powershell
python tools/nn2ctl.py run --rate 5 --seconds 10 --led --acquisition --ack-short-warning
```

Startup is: LED prepare/verify all outputs OFF and allocate I2C/timer while ADC is
stopped -> ADC initialize/qualify -> enqueue LED timer activation under the
cancellation interlock -> consume/optically pair real records. The LED owner
alone owns its hardware. Deferred activation performs no PCA initialization,
bus recreation or resource allocation. Cancellation invalidates pending
activation and asserts emitter reset; no preparation may restart after cancel.
The existing immediate LED-only START path remains available in its own profile.

## Memory, evidence and limits

The ADC static allocation ceiling is explicitly32KiB for128 event slots,256 raw
queue slots,existing logic/control storage and history. The previous13KiB ceiling
is superseded; actual linked RAM/IRAM use and stack margin remain NOT RUN.
The host-compatible private logic ring remains allocated but is unused by the
production queue publisher. No new dependency, heap sample ring or historical
file change is introduced.

Regression sources update command/calibration readbacks, FIFO event acceptance,
raw-buffer fatal overflow, publication failures, history/quality handling and
LED prepare/activate/cancel behavior. They have NOT been compiled or executed.
Static review, diff whitespace and historical SHA256 integrity are the only
checks performed here. Required hardware/RTOS review and board verification
remain pending. Roll back by reverting this uncommitted active-file candidate;
never alter the historical snapshots or erase board data as part of recovery.

## Boot59 follow-up: first continuity-uncertainty snapshot

The Owner reported a completed dark run with1024 qualification samples at8000SPS,
period123..127us,78955 produced /78933 consumed /22 STOP-tail records, no counted
runtime loss/overflow/late reads, but unknown_total=1. This is runtime success,
not proof of uninterrupted physical sampling. A first-uncertainty snapshot now
captures whether period>188us, incomplete channel freshness, or both caused that
nonfatal indication. It retains the preceding/current read timing and status,
observed serial, session and CRC-check completion timestamp. It is captured only
once per START and survives STOP; the next START clears it. ads131_unknown and
ads131_unknown_trace are emitted only after conversions stop/fault, never per
sample. Existing fault logs continue to cover fatal unknown-loss paths.

The existing32KiB static ceiling remains; additional snapshot storage is bounded
and actual linked size remains NOT RUN. ADC behavior, acceptance thresholds,
quality flags and test command are unchanged. Regression sources cover period,
freshness,both causes,first-event retention and STOP retention; they have not been
compiled or executed. No build, commit,push or hardware action is performed.

## Local split-latency diagnostic candidate

The Owner requested deeper diagnosis after a repeated boot62 dark run showed a
154us ISR-timestamp-to-transport delay on the preceding read, followed by a191us
recorded period. This patch changes instrumentation only; it does not weaken
CRC/status/continuity policy or enable LEDs. No firmware build, commit, push or
hardware operation is performed.

Each trace now records the owner timestamp immediately after its notification
wait returns, service entry, event-pop completion, transport entry/completion,
and polling-start API call/return. The ISR timestamps entry before its spinlock
and records entry-to-event-publication time in the existing event-record padding.
These timestamps remain software observations. A wait-return marker is not a
scheduler trace: one return can serve several events, or precede a later event.
resume_before_edge explicitly identifies the latter case. Driver call/return
are API boundaries, not measured CS/SCLK edges. ISR completion after notification
is not measured by isr_publish_us, which ends at publication preparation.

The first4 anomalous runtime reads retain their previous/current records, while
latency_seen counts all matching reads. Informational trigger flags are:1 period
>188us,2 incomplete freshness,4 ISR-entry-to-transport >100us,8 pop-to-transport
>30us,16 transport >80us. Multiple flags can apply; these do not add faults or
increment unknown-loss counters. Existing unknown rules still act independently.
Qualification/settling are not added to this anomaly set. First-unknown and fatal
history now include the same split timing. Buffers reset on successful START,
survive STOP and are printed only after STOP/fault. Existing fault history keeps
its prior first-fault freeze policy.

New logs: ads131_latency_summary,ads131_latency_event,ads131_latency and
ads131_latency_spi. Startup transport log identifies timing=split4. The dark
command stays `python tools/nn2ctl.py run --rate 5 --seconds 10` without LED or
optical acquisition flags. Collect all new latency lines alongside existing
unknown/rate/drain logs and STATUS, including runs that do not increment unknown.

The explicit ADC static-memory ceiling is36KiB, superseding32KiB for this bounded
instrumentation. The event ring item remains16bytes; richer history and4 frozen
pairs add fixed internal storage, no dynamic sample allocation. Actual linked
RAM/IRAM and timing overhead are NOT RUN. Extra timer calls can perturb timing;
compare runs as instrumented observations, not proof of unchanged scheduling.
Regression sources cover bounded diagnostic capture with unchanged quality and
the actual transport's driver-call boundaries. They have not been compiled or
executed per Owner instruction. Source review, diff checking and historical
integrity are the only validation here. If these boundaries remain ambiguous,
physical DRDY/CS/SCLK capture is the next diagnostic step.
