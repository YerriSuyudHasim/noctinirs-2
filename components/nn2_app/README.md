# nn2_app (Application layer)

Acquisition sequencer state machine `IDLE -> LED_ON -> SAMPLE -> LED_OFF -> IDLE` (+ `FAULT`), with all hardware reached through injected ports (`nn2_ports`). No ESP-IDF/FreeRTOS includes; `REQUIRES nn2_core nn2_ports`.

- Any port error: LED `off()` is issued on that exit (fail-safe), then state is `FAULT` and `faults++`. A failed fail-safe `off()` is logged and counted in `off_failures`. The original error is returned.
- `reset()` issues `off()` and returns to `IDLE` only when it succeeds, which also clears `last_error`. If `off()` fails, `reset()` returns that error and the sequencer stays in `FAULT`.
- Each sample carries `src` (provenance) copied from the adc port: `FAKE`, `ADC`, or `UNKNOWN` (zero-init).
- Caller context: one owner task, never an ISR. All on/off/read/log/delay callbacks run synchronously in the caller. Finite completion and bounds depend on their contracts; `settle_us` is a request, not an enforced wall-clock bound. Timing remains unmeasured. Cleanup runs synchronously in the caller task.
- Completed samples are pushed to a caller-owned `nn2_core_ring_t`; ring-full is drop-and-count, not a fault.
- Single-context; no synchronization (RTOS review item Q-4).

`nn2_app_safe_boot` is the issue #49 boot policy: record the reset reason, hold RESET#, and run one PCA9956B off sequence through `nn2_port_emitter_safe_t`. It does not create tasks or include ESP-IDF. A failure stays fail-closed with RESET# asserted. Host tests: `test/host`.

Status: DRAFT (M1). Host tests: `test/host`.

Concrete ownership, lifecycle, statuses, overflow, timing and acceptance gates: [Issue #13 contract](../../docs/ISSUE13_COMPONENT_CONTRACT.md). Opaque caller storage uses typed memcpy snapshots; bounded Architect/RTOS delta review remains required, with no author self-approval.
