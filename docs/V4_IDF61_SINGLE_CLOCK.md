# V4 single clock handoff

Owner direction 2026-10-29 23:54 KST: the ADS131M04 sample index is the master clock.
Owner direction 2026-10-30: this branch is a flash-ready test build. Bench checks are
visual and this timing report only. No human subject. No flash and no merge from
this PR. Class H is **NOT RUN**. Safety work and TWDT changes are out of scope.

Base is `feat/35-v4-idf61-restart` at `6da5e00` (PR #77, squash-merged).
`CONFIG_ESP_TASK_WDT_PANIC` stays `n`. Do not force-push a shared branch.

## Clock

DRDY sample index `n` (`uint32`, wraps) is the only LED and classification clock.
`fs = 8000` (`T = 125 µs`). The 1 MHz GPTimer stays for lateness, backlog age, and
the existing DRDY-stall path. It does not arm LED edges.

At 10 Hz a slot is 40 samples and a frame is 800. The 2200 µs OFF phase rounds to
18 samples. Settle is 4 samples (3 sinc3 periods from TI SBAS890D plus the
unmeasured 1-sample front-end placeholder in ADR-0004 N9). Guard is 1 sample
because 100 µs is shorter than one sample. ON stable is 14 and OFF stable is 17
when the commit is on time, both above `K` 10/8.

The session origin is the first DRDY the scheduler sees after START, plus
`lead = 2` samples. The scheduler is notified from the DRDY ISR and queues the
existing 7-byte ON or 2-byte OFF to the LED task. I2C is not inside the 125 µs
ADS loop. A late commit moves the stable window later and is counted. It is not
relabeled as stable. Task priorities are unchanged (ADS 23, LED 22, sched 15 on
core 1).

The frame timestamp is the GPTimer reading of the DRDY whose index is that
frame's slot-0 edge (ADR-0004 §3.16.c). Host time is `anchor / 8000` plus the
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

## Flash and look (Owner runs this; the agent does not)

Build from `firmware/v4-idf61` with ESP-IDF 6.1, target ESP32-S3. Do not flash
from the repository root.

```text
cd firmware/v4-idf61
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
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
