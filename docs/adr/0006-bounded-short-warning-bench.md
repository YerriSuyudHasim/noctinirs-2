# ADR-0006: Bounded SHORT-warning bench candidate

Date: 2026-10-27. Status: software candidate implemented; optional profile disabled by default.
Physical qualification and activation approval remain pending.
Not an approved safety exception, a verified electrical diagnosis or
authorization to flash/energize. Owner requests an alternative without opening
the permanently assembled board. V2/V3 remain the tested historical baselines.

## Scope, authority and evidence

See ../PCA9956B_SHORT_POLICY_REVIEW.md for the Owner's boot9 report: all20 active
channels sampled and SHORT, no OPEN/invalid code/deadline miss; observed light;
MODE2=0x45. The5.3V/low-VF detector explanation is plausible but unmeasured.
Software cannot distinguish a genuine short from that operating point.

Accountable activity: local firmware implementation. Required reviews: Hardware
(changed automatic fault response and limits), RTOS (timer cap and transitions),
independent nonauthor review, and the specific human authorization/procedure
required by AGENTS.md before activation and energized testing. These are pending.
No protection setting on the running device is changed by preparing this code.

Scope: a separate opt-in bench policy, classifier and tests, firmware-enforced
run limit, visible capability/status and CLI acknowledgement. Exclude ordinary
acquisition, production use, physical probing, changing supplies/currents/Fs,
clinical claims, auto restart, flash, push or merge. No reference edits or new
external dependencies. No assumption that a2-second limit qualifies exposure.

## Decision

Keep default strict behavior. Add CONFIG_NN2_LED_SHORT_BENCH, default n and
dependent on real LED bring-up. Ordinary LED bring-up and existing configs do not
select it. A separate defaults overlay is a review candidate, not applied to
the current root sdkconfig. The bench accepts only Fs5 and all20 currents30mA.
It permits only one accepted START attempt per firmware boot, even if cancelled
or cleared. Reboot is deliberate operator recovery, not automatic resumption.

At each completed frame the warning policy requires a fresh EFLAG sample for
every configured active channel in that frame. OPEN, reserved code, missing
samples, an unexplained ERROR, overtemperature, mode mismatch or I2C/readback
failure remain fatal. With ERROR set, only the complete expected all-active-
channel SHORT pattern may be advisory. Partial SHORT patterns are not accepted.
No CLRERR write conceals the indicator. Raw evidence remains cached and warning
frames are counted. This intentionally relaxes the response to a genuine short
with the same signature, so it is not an equivalent safety protection.

The existing GPTimer callback caps the attempt at2,000,000us from timer start,
independent of client duration/heartbeat or main/network polling. Deadline and
I2C guards still apply. On cap expiry it asserts RESET low, latches a fault and
records led_limit_hit=1 without claiming a deadline miss. It never resumes on
its own. A hung/masked ISR or CPU failure remains outside this software bound.
No new task/timer/queue or watchdog feed is added. The configured2seconds is a
software engineering ceiling, not a measured or approved thermal/optical limit.

INFO advertises short_policy=BENCH_WARN, limit_s=2 and single_run=1. USB STATUS
reports policy, warning-frame count, frame epochs, first stop cause, cap hit,
dispatch/completion maxima, image identity and cleanup/bus state. CLI requires both --led and
--ack-short-warning and rejects run durations over2seconds for that capability.
The CLI limit is convenience only; firmware is the enforcement. Direct protocol
clients remain unauthenticated. Recommended first reviewed candidate run is1s,
not a10-second scan. Strict mode has no new time/current/Fs restriction.

## Acceptance and limitations

Host tests must prove strict still faults on the reported signature, bench can
finish multiple frames with that signature and retain warnings, all other
faults remain stops, stale/partial evidence cannot authorize continuing, zero
or changed currents/other Fs are rejected, cancellation still prevents rearm,
the cap works without host STOP, and a second START is denied after completion
or clear-fault. Native tests and static target probes are not timing, electrical
or safety qualification. Full builds/maps/size, target timing and independent
review remain required. No actual electrical/optical fault is declared absent.

Activation requires a separately reviewed decision on this known limitation
and an approved isolated nonhuman bench procedure with energy disconnection,
limits and stop conditions. If sufficient safety evidence cannot be obtained
without opening the board, the bench remains disabled; software cannot replace
that evidence. Long-term options include compatible electrical headroom or
independent fault/current sensing, outside this change.

Rollback: build the matched source/client with the bench option unset; the
strict shutdown remains the default. Source or configuration changes alone do
not alter flashed firmware or establish off. No partitions/security/eFuses or
charger settings are changed. Preserve evidence before recovery/clearing.

## Implementation addendum (2026-10-27)

Implementation authority: [Agent03 assignment](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6439754275).
This supersedes the prior sequencing of measurements before software work.
The Owner confirms no historical current/voltage records exist. High sink voltage
is the design premise; it is not a measurement or proof that no real short exists.
Software tests proceed now. Physical qualification remains a separate gate.

Fresh frame scratch is cleared on slot0 and receives an incremented epoch. Each
active channel is sampled at its OFF boundary while still ON, before the full
zero LEDOUT image. Only that channel's two bits are merged. Complete and first
fault snapshots have separate storage; reset/cleanup/clear-fault retain them.
A repeated health classification without a new frame is rejected. ERROR-clear
with no SHORT is healthy; ERROR-clear with SHORT is inconsistent and fatal.
ERROR-set is advisory only with valid=SHORT=0xFFC3FF, OPEN=invalid=0 and matching
MODE1/MODE2 configuration. All other ERROR cases fail. OPEN/code3 stops at the
observed sample, without waiting for frame end. MODE1/MODE2 are read at frame
end; their nominal Fs5 observation interval is200ms and still needs RTOS/Hardware
disposition. Autonomous chip thermal protection is separate from this polling.

[NXP Rev1.2 section7.3.14](https://www.nxp.com/docs/en/data-sheet/PCA9956B.pdf)
requires eligible PWM and ON conditions for diagnosis. Prepare now writes and
reads back PWMALL=255, because PWM<8 suppresses detection. This does not change
FULL_ON current or one-hot selection. REXT remains1kOhm; unused IREF and LEDOUT
remain zero. EFLAG is sampled after the existing ON interval, rather than
immediately following ON; no new blocking delay is inserted. The datasheet gives
no quantitative guaranteed EFLAG settling maximum here. Actual settling and edge
timing remain physical review gates; host data prove only the model.

The ISR uses current GPTimer count rather than cached callback count, schedules
min(next phase,2,000,000us), and gives expiry priority at equality. Worker entry,
each bus operation and completion check expiry. Cancel/reset-release/rearm share
the critical-section interlock. A late completion cannot release reset or
publish a successful phase. The cap starts when the timer starts, after prepare
with all outputs OFF. Preparing does not authorize a light pulse. A hung CPU or
masked interrupts remain outside the software bound. The ISR's count/control
calls and reset path require exact-image IRAM inspection and target measurement.

No per-frame warning log, network packet or UI notification is introduced.
INFO adds a static port capability string, keeping platform configuration out of
application policy. USB STATUS uses one locked LED snapshot and cached data;
it adds no I2C. The bounded serial buffer/reader grows from1024 to1536 bytes to
hold identity and telemetry. Existing root configuration is not changed.
See [implementation handoff](../SHORT_ADVISORY_HANDOFF.md) for commands, results,
source identity, remaining gates and review requests.
