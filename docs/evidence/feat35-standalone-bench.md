# Feat35 ADC standalone checkpoint

Recorded 2026-10-29 KST from Owner-provided serial/nn2ctl output.
Accountable implementation activity: issue35 firmware author work. Required
independent hardware/RTOS review and merge approval remain pending.

## Source and scope

Base commit: b0eb800336341b4b295ffad1aaa130e15457d2dd on
feat/35-ads131-acquisition. The commit containing this record preserves the
current local ADC-only candidate on feat/35-ads131-standalone. Git branches are
peers; this branch descends from the recorded feat35 base and is not a merge.

The acquisition overlay defaults to NN2_ADC_V4_DARK_BASELINE=y. INFO advertises
led=OFF;baseline=V4_DARK;frames=unsupported. CPU240MHz,16MiB flash, V4-derived
register/transport/event/sample ownership and bounded deferred diagnostics are
included. LED prepare/activate scaffolding is also present from the local work,
but it is disabled in the default dark path and has not been board-qualified.
See ../FEAT35_V4_BASELINE.md for detailed settings and test procedures.

This checkpoint does not claim optical/LED integration acceptance, physical
sample-continuity proof, production readiness, independent review or permission
to merge/release/flash. Historical V2/V3/V4 snapshots are unchanged.

## User-reported bench observations

Earlier boot62 dark run completed STARTING -> STREAMING -> STOPPED, fault0.
Qualification:1024 samples,8000SPS,period124..126us,zero qualification gaps.
Totals:78954 produced,78934 consumed,20 STOP-tail records,zero loss,overflow,
unknown-loss indications and late reads; maximum raw depth24/256.

The subsequently instrumented dark run reported:

| Metric | First supplied split-latency run | Following60-second run |
| --- | ---: | ---: |
| Qualification rate | 8000SPS | 8000SPS |
| Qualification period range | 119..131us | 124..125us |
| Produced total | 158958 | 637919 |
| Consumed total | 158933 | 637869 |
| STOP-tail discarded total | 25 | 50 |
| Counted loss / overflow / unknown / late reads | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| Maximum raw depth | 32 | 32 |
| Informational latency events / retained | 20 / 4 | 62 / 4 |

Across those two totals:478961 produced =478936 consumed +25 STOP-tail records.
The four retained latency events in each log have flags8 (pop-to-transport
software time over30us), valid010f status and no reported continuity uncertainty.
The later logs do not include a firmware image hash or boot identity, so they
cannot independently attest the precise precommit binary/source mapping.
A prior run did report period-related uncertainty and a154us pre-transport delay;
these later clean observations do not prove that intermittent behavior is gone.

These are Owner-reported on-target bench observations. The agent did not build,
flash or operate hardware, and did not independently measure DRDY/CS/SCLK.
ESP32-S3/ESP-IDF6.1 and the selected defaults describe the source target; exact
Owner build toolchain, linked memory, binary size and map evidence for these
runs were not supplied here. Preserve that distinction when reviewing results.

## Checkpoint validation and exclusions

Before committing:git diff --check and tools/verify-reference.ps1 pass (319
historical files). New/updated host and LED regression sources have not been
compiled or executed by the agent, per the Owner's no-build instruction. GitHub
checks for the new commit are not claimed in this record. Board review,
long-duration/fault-injection tests and LED coexistence acceptance remain open.
Only active source, versioned configuration, test sources and documentation are
included. Generated sdkconfig/build artifacts, binaries and local tool installs
are excluded. Rollback is switching to the recorded feat35 base or reverting
the checkpoint on an appropriate branch; no board erase or reset of user data.
