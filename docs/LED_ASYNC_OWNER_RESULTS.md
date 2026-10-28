# Owner-reported async LED results: Fs5 and Fs10

Recorded2026-10-28 KST by Agent03 Firmware Developer, OpenAI Codex,
session agent03-async-20261028. Assignment: [issue33](https://github.com/YerriSuyudHasim/noctinirs-2/issues/33#issuecomment-6439754275).
The Owner performed the builds, flash and runs. The agent only inspected supplied
logs and cached USB STATUS. This is Owner-reported functional bench evidence,
not an independent verification sign-off or electrical/optical qualification.

Tested source commit: `197134ec0fd72363e4e814927812c4d48417ddf0`.
Reported ELF/image SHA256: `172181776d967a2d96dcc120d3699dbed1e53ec6cfc28368f49cfd8dcb1afb0f`.
Both runs used boot23, BENCH_WARN, ASYNC@800000, HOST duration, no cap/quota,
and the Owner's same assembled main board; exact board revision was not supplied.
The configured framework is ESP-IDF6.1. Later evidence-only commits do not change
the tested firmware implementation; a later rebuilt image may have a new identity.

## Results

| Metric | Fs5,10s | Fs10,10s |
| --- | --- | --- |
| Session |2096752712 |2096752713 |
| Final state / fault | STOPPED /0 | STOPPED /0 |
| Complete frames / edges |49 /1996 |99 /3993 |
| Epoch / last complete epoch |50 /49 |100 /99 |
| Deadline misses / LED error |0 /0 |0 /0 |
| Phase completion maximum |670us |806us |
| Phase budget |5000us |2500us |
| Observed margin against budget |4330us |1694us |
| Async submitted / completed |3043 /3043 |6088 /6088 |
| Async errors / stale callbacks |0 /0 |0 /0 |
| First stop / bus released |1 (normal STOP) /1 |1 (normal STOP) /1 |
| Complete advisory warning frames |49 |99 |
| OPEN / invalid EFLAG |0 /0 |0 /0 |
| Owner stack-free telemetry |4488 |4488 |

Both runs progressed STARTING -> STREAMING -> STOPPING -> STOPPED with fault0
and no host lease expiration. The second session confirms same-boot restart
following completed STOP cleanup. The Fs10 observed completion maximum uses
32.24% of the2500us phase budget; this is the maximum reported for this run,
not a bound on future worst-case execution or a waveform-based timing guarantee.

## Partial final frames are consistent with host STOP

Fs5:1996=49*40+36 edges, i.e.49 complete frames plus18 completed ON/OFF pairs.
Its transaction count is49*61+18*3=3043. The partial epoch50 mask3FC3FF
contains18 sampled channels; channels22 and23 had not been sampled before STOP.

Fs10:3993=99*40+33 edges, i.e.99 complete frames plus16 completed ON/OFF pairs
and one more ON. Its transaction count is99*61+16*3+1=6088. The partial epoch100
mask0FC3FF contains16 sampled channels through hardware channel19. Channel20
was turned ON in the partial frame but its EFLAG read/OFF phase had not completed
when STOP asserted reset. This explains the partial mask without a missing
sample fault; completed frames had already passed freshness checks.

MODE2=45 and the SHORT warnings remain consistent with the explicitly selected
board-specific advisory. They do not establish that the SHORT indications are
false positives. No OPEN/reserved EFLAG, deadline miss or I2C error is reported.

The first STATUS in the earlier combined paste was the preceding Fs5 snapshot.
The subsequent inline Fs10 STATUS has99 frames and6088 transactions and belongs
to the completed Fs10 run. It supersedes the earlier missing-STATUS note. A
trailing repeated command without replies is not counted as another successful run.

## Evidence and remaining review

[Machine-readable evidence](evidence/owner-led-async-2026-10-28.json) preserves
supplied protocol replies, both raw STATUS lines, attachment hashes and arithmetic.
No additional RUN, reset, build or device access was performed to obtain it.
Only documentation/evidence changes follow the tested source commit.

This supports a draft PR and records functional Fs10 operation for10 seconds.
It does not satisfy all merge gates: required CI, Hardware review of800kHz and
SHORT classification, RTOS review of ISR/priority/drain, non-author native test
execution, independent review, and human merge approval remain pending. Actual
SCL waveform, electrical/thermal/optical qualification and sustained-runtime
results remain outside these two reports. Agent firmware build/link/size/map,
flash and energized testing remain NOT RUN per Owner instruction.
