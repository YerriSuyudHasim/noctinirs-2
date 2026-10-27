# Owner-operated communication acceptance procedure

> SUPERSEDED PROCEDURE (2026-10-27): use
> [ADR004_OWNER_BENCH_GUIDE.md](ADR004_OWNER_BENCH_GUIDE.md) for current build,
> physical entry gates, SoftAP/ADR-0004 commands and report template. The historical
> STA/CLAIM and silence-timeout instructions below are not current acceptance steps.

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

Status: NOT RUN. This is a procedure, not authorization or a test result. Record
physical board revision, wiring/fixture, firmware source fingerprint, SDK/toolchain,
generated configuration and network setup before execution. Build success alone
does not satisfy any electrical criterion.

## Preconditions

1. Build the isolated ESP32-S3 communication profile using v6.1; record complete
   output, warnings, flash/RAM/binary size. Resolve unexplained warnings first.
2. Confirm the assembled hardware matches GPIO18 -> RESET_LED -> PCA9956B RESET#.
   Inspect actual pull/reset behavior, connectors and emitter energy isolation.
   Keep emitters unable to energize until reset effectiveness has been measured.
3. Use a controlled WPA2 LAN and host with synchronized local test logging. Record
   reset pin voltage with a suitable instrument across power-on and restart.
   Software reports `off_requested`, never `physically_verified_off`.

## Limits selected before execution

These are initial engineering acceptance targets, not measured guarantees. Keep
the same limits for a run; a change requires a recorded reason and a fresh run.

| Check | Acceptance target |
|---|---|
| Emitter inactivity | No observed activation on boot/reset, commands, timeout or link loss. Voltage interpretation must follow actual board logic limits. Any activation fails. |
| 30-minute traffic soak | 10 valid request/reply transactions/sec on a local LAN; unique IDs, correct CRC/opcode/state, no incorrect execution |
| Response latency | p95 <=100 ms, p99 <=250 ms; no healthy-link response >1 s after configured retries |
| Healthy-link loss | <=0.1% original attempts require retry; zero unresolved transactions after at most 2 retries at 500 ms |
| Heartbeat loss | Software simulation leaves RUNNING by 3.2 s after last new heartbeat; reset remains asserted throughout |
| Link recovery | 10 AP/network interruptions; usable UDP within 60 s after AP/DHCP becomes available; new CLAIM required |
| Restart persistence | 10 board restarts: all retain credentials, boot ID advances, state is IDLE, no unexpected reset reason |
| Memory/task health | No allocation failure/watchdog/panic; main stack watermark >=2048 bytes; no sustained free-heap decline >4 KiB between minutes 5 and 30 |
| Secret handling | No password or encoded password in logs/STATUS/responses. Leakage fails. |

The reconnect ceiling allows capped 32-second backoff plus DHCP/association time.
Latency is a communication usability target; it is not a sampling timing claim.

## Execution sequence

1. Load the candidate only after the Owner approves board operation. Collect boot
   logs and reset-pin evidence. Check no LED/ADC driver is initialized by this profile.
2. Provision by USB with the CLI. Verify valid storage acknowledgement, malformed
   hex/too-short/too-long rejection, no input echo and STATUS IP acquisition.
3. Restart and confirm persistence/boot ID. Replace credentials and verify the
   old running connection is not silently changed; restart to apply replacement.
4. Run INFO/STATUS/GET_TIME/config/start/heartbeat/stop/safe-off/fault/clear-fault.
   Every hardware-dependent result must remain SIMULATED. Replay an identical
   request ID; check identical response and no repeated transition. Conflict IDs
   must fail. A second endpoint must not steal the active lease.
5. Stop new heartbeats while sending other queries/duplicate heartbeats. Verify
   the 3-second expiry and no busy wait. New CLAIM is necessary to resume.
6. Perform the 30-minute soak, record each ID, first-send/receive monotonic times,
   retries, timeouts, errors and device boot/time. Send USB STATUS every 10 seconds
   for heap, watermark, reset and transport counters. Exclude retry RTT from clock
   offset estimates, but include its latency in overall transaction statistics.
7. Perform ten network interruptions with timestamps, then ten restarts. Compare
   recovery time, boot ID, retained credentials and reset-pin trace for every case.

## Failures and recovery

Unexplained reset, emitter activity, incorrect state/execution, missing persistence
or leakage fails immediately. Disconnect emitter energy, preserve logs/source/
configuration and investigate. Do not use global NVS erase as an automatic repair.
No factory reset, firmware signing, secure-boot/eFuse change or optical validation
is included. Record each result as on-target bench evidence, separate from host
and static evidence. RF/board operation has not been performed by this agent.
