# V4 single clock handoff

Owner direction 2026-10-29 23:54 KST: the ADS131M04 sample index is the master clock.
Owner direction 2026-10-30: this branch is a flash-ready test build. Bench checks are
visual and this timing report only. No human subject. No flash and no merge from
this PR. Class H is **NOT RUN**. Safety work and TWDT changes are out of scope.

Base is `feat/35-v4-idf61-restart` at `6da5e00` (PR #77, squash-merged).
`CONFIG_ESP_TASK_WDT_PANIC` stays `n`. Do not force-push a shared branch.

## Clock

DRDY sample index `n` (`uint32`, wraps) is the only LED and classification clock.
`fs = 8000` (`T = 125 Âµs`). The 1 MHz GPTimer stays for lateness, backlog age, and
the existing DRDY-stall path. It does not arm LED edges.

At 10 Hz a slot is 40 samples and a frame is 800. The 2200 Âµs OFF phase rounds to
18 samples. Settle is 4 samples (3 sinc3 periods from TI SBAS890D plus the
unmeasured 1-sample front-end placeholder in ADR-0004 N9). Guard is 1 sample
because 100 Âµs is shorter than one sample. ON stable is 14 and OFF stable is 17
when the commit is on time, both above `K` 10/8.

The session origin is the first DRDY the scheduler sees after START, plus
`lead = 2` samples. The scheduler is notified from the DRDY ISR and queues the
existing 7-byte ON or 2-byte OFF to the LED task. I2C is not inside the 125 Âµs
ADS loop. A late commit moves the stable window later and is counted. It is not
relabeled as stable. Task priorities are unchanged (ADS 23, LED 22, sched 15 on
core 1).

The frame timestamp is the GPTimer reading of the DRDY whose index is that
frame's slot-0 edge (ADR-0004 Â§3.16.c). Host time is `anchor / 8000` plus the
session epoch. Group delay is not subtracted.

## Timing report

`nn2ctl timing` appends, after the existing `v4timing=1` fields:

```text
fs=8000 anchor=<sample> frame_delta=<samples> slot_samples=40 off_samples=18
missed=<adc_missed> catchup=<adc_backlog_catchup> gap=<adc_sequence_gap>
edge_miss=<count> on_d=<20 deltas> off_d=<20 deltas>
```

`on_d`/`off_d` are `actual - planned` in samples, or `x` if that commit has not
been seen. Planned ON for slot `s` is `anchor + s * slot_samples`. Planned OFF
adds `off_samples`. `frame_delta` is the sample-index gap between consecutive
slot-0 DRDY anchors (800 at 10 Hz when none were dropped). `nn2ctl` puts the
expanded per-slot planned/actual indices in `sync`.

The V4 optical datagram version 2 also carries the measured anchor,
`first_sample_sequence`, and `adc_fs_hz`, and `REC_TIME64` is little-endian.
INFO/health still say `frames=unsupported`. That socket does not emit ACQ1.

## What is not done

- Class H logic-analyzer check. **NOT RUN.**
- Binding optical frames onto UDP 3333 as NN2 `FRAME` messages.
- Any TWDT, watchdog, brownout, D6, current-cap, or pin change.

## Authorized bench procedure

Use ESP-IDF 6.1, target ESP32-S3. Select the V4 project explicitly when
invoking IDF from the repository root. Flashing requires owner authorization;
the owner authorized the PR78 COM11 bench session on 2026-10-30.

```text
idf.py -C firmware/v4-idf61 build
idf.py -C firmware/v4-idf61 -p PORT flash monitor
```

On the bench, no human subject, emitters only if the approved procedure allows
`--led`:

```text
python tools/nn2ctl.py info
python tools/nn2ctl.py timing
python tools/nn2ctl.py run --rate 10 --seconds 10 --led
python tools/nn2ctl.py timing
```

Read `sync`. `frame_delta` should match `slot_samples * 20`. `on_d`/`off_d`
should be 0 or a small late count, not a walk that grows over the run.
`missed` and `catchup` should stay at the pre-run value. Watch one LED against
DRDY if a logic analyzer is already on the bench. Stop with `nn2ctl stop`.


## PR78 bench fixes, 2026-10-30

Author validation by Codex, session 01a18b70-d855-73f3-a7bd-4f9e8ffd8705,
under the owner's explicit task request. Independent review remains pending.
Runtime source tested: commit `95deb67` (the subsequent documentation/manifest
commit does not change runtime source). ESP-IDF v6.1
`fff9895c82d744c7237be8847347bdd1b07c6643`, Xtensa GCC 15.2.0,
ESP32-S3 revision 0.1, 16 MB flash, COM11; board PCB revision unknown.
Local configuration uses DEBUG optimization, 240 MHz, and TWDT panic disabled.

### Cause and implementation

Fault 267 is protocol `0x010B FAULT_OPTICAL`; it is not an ESP-IDF error code.
The scheduler's underlying timeout was caused by excessive software pipeline
latency. ISR-safe driver configuration alone did not place the application
LED/ADS/scheduler tasks or task-context I2C submit code in RAM. Controlled
instrumented trials still faulted with compiler optimization and one-shot
wakes; changing linker placement of those objects plus I2C master/HAL to
`noflash` produced the first passing run. This supports flash/cache-sensitive
execution as the bottleneck; cache-miss counts and physical bus waveforms
were not measured. The cleaned DEBUG build also passes, without the temporary
CRC, batching, optimization or profiling experiments.

Separately, the old timing report read the current DRDY index when the
scheduler consumed a completion, including task-delivery delay in the edge
index. The I2C completion ISR now freezes sample index and generation before
publishing the completion. Generation checks reject stale data. DRDY updates
continue every sample, while the scheduler receives one wake per armed target.
A small state snapshot replaces full diagnostic status copies in deadline
handling. Legacy detailed submit histograms remain available for diagnostic
calls; scheduled edges retain cheap submit/completion timestamps.

`critical_paths.lf` trades SRAM for bounded execution residency. The final
image loads 147304 bytes of IRAM code; review the linker map and SRAM budget
when adding code. CI checks the required task/handoff/submit symbols in the
ELF. Placement checks establish residency, not physical timing guarantees.

The owner SPI fix uses `portMAX_DELAY` as required by IDF 6.1. Audit of the
installed SDK found SPI acquire and polling-start require this value;
remaining active calls already comply. Polling-end and I2C finite waits are
supported and retained. No SDK patches are required.

### Diagnostics and regression coverage

Health exposes `async_latency_min_us`, `async_latency_max_us`,
`async_latency_avg_us`, `async_on_pending_at_submit`,
`async_off_pending_at_submit`, `sample_clock_late_commits`,
`led_completion_timeout`, `sched_error` and `adc_error`.
`i2c_on_us`/`i2c_off_us` contain the last accepted submit-to-completion duration
for each of 20 slots; unknown values are `x`. Formatting happens on query.
`nn2ctl` prints numeric fault plus `fault_name`, and expands these durations
into `async_i2c`. Durations include software overhead, not just wire time.

Host validation: 136 tests pass, including real production sample-edge code
with SDK mocks, delayed completion delivery, one-shot wake, generation reset,
anchor latching and sequence wrap. Assertion-failure and zero-test controls
both fail as intended. Thirteen Python diagnostic/placement regression tests
pass. Migration/provenance checks and all 319 historical reference hashes pass.
Local full architecture testing uses legacy MinGW GCC 5.1; its C17 compiler
fixtures cannot establish the modern Linux CI result. CI is reported separately
on the exact pushed revision.

### Final on-target result

After root-level `idf.py -C firmware/v4-idf61 -p COM11 flash`, run
`python tools/nn2ctl.py run --rate 10 --seconds 10 --led` completed STREAMING
and STOPPED with fault 0 (boot 131). Health/timing after stop recorded:

- 8000 samples/s, 40 samples/slot, OFF at sample 18, frame_delta 800.
- All 20 ON and OFF deltas +1 sample.
- Missed, catchup, sequence gap, edge_miss, CRC and SPI errors: 0.
- Async latency min/max/avg: 66/223/177 us; pending-at-submit and completion
  timeout: 0. `sample_clock_late_commits=3864` counts positive quantized
  commits, including the accepted +1 sample edges; it is not an edge-miss count.
- ADC OK 77276. Serial boot after monitor reset (boot 132) confirms project
  `NoctiNIRSv4_IDF61` and `BOOT_STAGE stage=ready ... ads=1 runtime=1 network=1`.
- Existing boot warning: SPI GPIO7 conflict/overwrite; electrical implications
  require Hardware Specialist review. It did not prevent this bench run.

This is one-board author bench evidence, not independent HIL or waveform
qualification. No logic analyzer, long-duration run, physical optical timing,
fault injection, or human-subject testing was performed. Runtime I2C 800 kHz,
SPI 8 MHz, ON 7-byte/OFF 2-byte payloads and safety/TWDT policy are unchanged.
Hardware Specialist, RTOS Specialist and independent review are required before
human merge approval. Roll back by reverting the PR78 fix commits and rebuilding;
no partition, bootloader configuration, security or eFuse change is introduced.

## Batch 1 diagnostics (local changes, 2026-10-30)

The supported optical frame rates are exactly **2, 4, 5, 8, 10 Hz**. INFO adds
`fs_allowed=2,4,5,8,10` and retains `fs_min=2;fs_max=10`. The host rejects other
rates before opening a client. For START/run and set-fs it also honors
`fs_allowed` when advertised; older INFO uses the host allowlist (and existing
BENCH_WARN range checks).

Both health and timing add `sample_clock_early_commits`, `late_1`, `late_2`,
`late_3p`, `late_max` (samples), `async_completion_errors`, `async_submit_errors`,
`async_stale_completion`, `async_callback_core`, `async_led_task_core`,
`scheduler_task_core`, `udp_frames_sent`, and `udp_frames_dropped`. The host
preserves these keys in `diag`, including signed core IDs (`-1` means unknown).
The core IDs are observations, not error counts; the three task/callback core
IDs normally report core 1. UDP counters describe the existing V4 stream, not
NN2 FRAME support.

The histogram combines ON and OFF and accumulates within one scan; START resets
it alongside the existing late/early counters. On-time and early commits do not
enter late buckets. `late_max` is the largest positive first-affected minus
planned edge delta; `late_1 + late_2 + late_3p` equals
`sample_clock_late_commits` at a quiescent snapshot. No division, allocation, or
logging is added to commit handling; ISR, ADS and LED task code is unchanged.

The control diagnostic buffer uses the existing NN2 payload budget (1412 bytes
of text plus NUL); no wire limit changes. Extreme counter widths or per-slot
lists may still exceed the single-datagram budget. Firmware build, flash,
600-second bench, timing performance and hardware qualification: **NOT RUN**.


## Batch 1b current policy (Owner decision D3)

`components/nn2_core/include/nn2_core_optical.h` is the firmware source of truth:
`NN2_OPTICAL_DEFAULT_MA=30` and `NN2_OPTICAL_MAX_MA=50`, with a compile-time
DEFAULT <= MAX assertion. V4 initialization and the communication configuration
use the 30 mA default. V4 control, LED driver, shared PCA adapter and application
communication accept 0..50 mA. `NN2_OPTICAL_BRINGUP_MA` remains a compatibility
alias of DEFAULT for the separate bring-up profile; its advisory mode still
requires exactly 30 mA and its port limit stays 30 mA.

INFO retains old keys and adds `current_default_ma=30;current_max_ma=50`, derived
from the shared constants. The host honors these limits when advertised (older
INFO falls back to 30/50), rejects out-of-range requests before sending a current
command, and rejects >50 before opening a client. START/run checks configured
per-channel currents; explicit `--current-ma` configures all channels using the
existing command. Currents above 30 mA print the requested irradiance warning
on stderr, without adding an acknowledgement flag or a new gate.

Conversion formulas are unchanged: with Rext=1000 ohm the V4 nearest-integer
conversion and shared adapter floor conversion both yield IREF codes
0, 67/66, 133/133, 222/222 for 0, 15, 30, 50 mA respectively. Code 222 fits
an 8-bit register; tolerances and irradiance require Owner assessment.

The scheduler already recorded its core at task entry, but init could overwrite
that observation with -1 after task creation. The unknown initialization now
precedes creation; no scheduling, priority or affinity change is made.

B3 deferred within the requested scope: legacy FNIRS_CFG has only two scalar
currents (735/850 nm), while NN2 sessions allow 20 independent channel currents.
The network receives completed frames without session current configuration.
Its existing hardcoded 15 mA metadata remains inaccurate; representing session
currents correctly needs a separate data-flow/format decision, not replacement
with another fixed value. Legacy frame_fs_hz is also fixed at 10 and is outside
this task. No network format or frame structure is changed.

ESP-IDF build: **NOT RUN**. Flash: **NOT RUN**. On-target bench: **NOT RUN**.
Owner bench is required before push. Host tests do not validate optical safety
or real-time hardware performance.

## Batch 4a diagnostics pointer

Batch 4a adds observation-only keys in a new read-only opcode `get_netdiag`
(0x0032): `heap_free_now`, `heap_free_min`, `largest_free_block_now`, `frame_send_*`.
`get_health` and `get_timing` are unchanged from the pre-Batch-4a build. A RAM budget
gate is also added. See [BATCH4A_DIAGNOSTICS.md](BATCH4A_DIAGNOSTICS.md) and
[RAM_BUDGET.md](RAM_BUDGET.md). Owner bench: **NOT RUN** in this environment.
