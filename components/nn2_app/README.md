# nn2_app (Application layer)

Acquisition sequencer state machine `IDLE -> LED_ON -> SAMPLE -> LED_OFF -> IDLE` (+ `FAULT`), with all hardware reached through injected ports (`nn2_ports`). No ESP-IDF/FreeRTOS includes; `REQUIRES nn2_core nn2_ports`.

- Any port error: LED `off()` is issued on that exit (fail-safe), then state is `FAULT` and `faults++`. A failed fail-safe `off()` is logged and counted in `off_failures`. The original error is returned.
- `reset()` issues `off()` and returns to `IDLE` only when it succeeds, which also clears `last_error`. If `off()` fails, `reset()` returns that error and the sequencer stays in `FAULT`.
- Each sample carries `src` (provenance) copied from the adc port: `FAKE`, `ADC`, or `UNKNOWN` (zero-init).
- Caller context: one owner task, never an ISR. The only wait is `clock.delay_us(settle_us)`, which is bounded. Cleanup runs synchronously in the caller task.
- Completed samples are pushed to a caller-owned `nn2_core_ring_t`; ring-full is drop-and-count, not a fault.
- Single-context; no synchronization (RTOS review item Q-4).

Status: DRAFT (M1). Host tests: `test/host`.

Concrete ownership, lifecycle, statuses, overflow, timing and acceptance gates: [Issue #13 contract](../../docs/ISSUE13_COMPONENT_CONTRACT.md). Public-struct representation requires explicit Architect assessment against ADR-0001; no exception is self-approved.
