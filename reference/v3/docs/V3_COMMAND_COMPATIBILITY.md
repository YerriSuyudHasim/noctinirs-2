# Command Compatibility

Commands are ASCII over UDP port 3333. Parsing and replies run in the core-0,
priority-9 command task. Commands never execute I2C recovery directly and do not
modify active slot timing.

| Command | Final behavior | Running policy |
|---|---|---|
| `PING` | Returns `PONG` | Allowed |
| `HELLO` | Reports control/stream ports and default mode | Allowed |
| `VERSION`, `INFO` | Firmware, phase, target, packet format | Allowed |
| `HELP` | Lists final command surface | Allowed |
| `STATUS` | Lifecycle plus independent network/acquisition state | Allowed |
| `HEALTH` | Full health snapshot/counters | Allowed |
| `TIMING` | Immutable timing and I2C profile | Allowed |
| `REGS` | Cached/readback diagnostic register state | Allowed; does not force runtime recovery |
| `DUMP_SLOT_TIMING`, `TRACE` | Formats latest timing history | Allowed; bounded output |
| `DARK`, `MODE DARK` | Selects next-start mode | Allowed; active timing unchanged |
| `SCAN`, `MODE SCAN` | Selects next-start mode | Allowed; active timing unchanged |
| `START` | Starts selected mode | Idempotent only when lifecycle/mode already match; otherwise explicit error |
| `START DARK` | Starts DARK | Same lifecycle policy |
| `START SCAN` | Starts SCAN | Same lifecycle policy |
| `STOP` | Acknowledgement-based safe stop | Idempotent when already stopped |
| `RESET_COUNTERS` | Resets runtime counters only | Stopped-only; preserves lifecycle/session identity |
| `CURRENT 735 <0..50>` | Changes 735 nm profile and rebuilds persistent payloads | Stopped-only, applies next START |
| `CURRENT 850 <0..50>` | Changes 850 nm profile and rebuilds persistent payloads | Stopped-only, applies next START |
| `SLOT <0..19>` | Returns immutable descriptor information | Diagnostic only; SCAN still uses all 20 slots |
| `MODE SINGLE`, `START SINGLE` | Explicitly unsupported | Returns clear error |

## START/STOP network barrier

Before START or STOP, the command task requests a bounded stream reset and waits
for an acknowledgement from the UDP stream task. START is rejected if this
barrier times out. STOP still proceeds to safe acquisition shutdown if the
stream barrier fails, while counting a stream error.

## Configuration update contract

- Timing values are compile-time/immutable for a running session.
- Mode selection is a next-start setting.
- Current profile changes are rejected while running.
- `config_generation` changes only when a new immutable configuration is applied.
