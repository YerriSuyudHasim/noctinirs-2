# ADR: Board power initialization and exclusive I2C management phases

Date: 2026-10-27
Status: Local implementation decision; hardware/RTOS and independent review pending

## Context

The Owner requested fuel-gauge communication, automatic charging enable at boot,
and battery telemetry every minute while idle and every five minutes during
acquisition. The current schematic connects MAX17048, BQ24295 and PCA9956B to the
same physical SDA/SCL wires. Existing communication acquisition is simulated.
ESP-IDF 6.1 asynchronous transfers change bus execution semantics; registering a
callback on a future LED device must not silently turn power reads asynchronous.

## Decision

Extend the existing platform `nn2_board` component with `nn2_board_power`, keeping
GPIO, SDK I2C handles, device identities and register configuration inside that
adapter. Add a pure `nn2_port_battery` value/read contract and an `nn2_app_battery`
monitor responsible for cadence, cached data, freshness timestamps and errors.
No dependency from core/application policy to ESP-IDF or hardware is introduced.

Power phases use synchronous, finite-timeout reads with one task owning the
complete bus lifecycle. They release I2C0 before another client may use the same
wires. A future real LED scheduler must explicitly grant an exclusive idle phase
after draining and deleting its own bus; periodic reads are postponed otherwise.
Two controllers must never be independently routed to the same physical wires.
No real LED scheduling or timing guarantee is implemented by this change.

Enable charging only after identity checks, host-watchdog configuration, charge
mode configuration and readback succeed. Keep the charging safety timer enabled;
preserve existing charge-current/voltage limits. These limits require board/cell
review before physical operation. No source constant is inferred from historical
firmware as an authoritative electrical specification.

## Consequences and alternatives

Synchronous reads simplify error propagation and stack-buffer lifetimes for very
infrequent telemetry. Deleting/recreating the bus requires measuring allocation,
latency, memory and GPIO effects before real acquisition integration. Errors may
leave ownership pending cleanup; the scheduler must not start the next phase
until cleanup succeeds. Cached telemetry can become older than five minutes.

Concurrent power reads during queued LED transfers are rejected. A mutex alone
does not enforce optical deadlines. A permanently synchronous shared bus with a
single sequencer is another possible future design, requiring review of its
scheduling contract. Strict telemetry deadlines during uninterrupted acquisition
require a separate physical bus or an approved acquisition pause.

See [BOARD_POWER.md](../BOARD_POWER.md) for schematic provenance, acceptance
criteria, detailed behavior, references, exclusions and rollback. See
[BOARD_POWER_EVIDENCE.md](../BOARD_POWER_EVIDENCE.md) for verification limits.
