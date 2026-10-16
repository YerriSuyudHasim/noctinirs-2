# nn2_app (Application layer)

Acquisition sequencer state machine `IDLE -> LED_ON -> SAMPLE -> LED_OFF -> IDLE` (+ `FAULT`), with all hardware reached through injected ports (`nn2_ports`). No ESP-IDF/FreeRTOS includes; `REQUIRES nn2_core nn2_ports`.

- Any port error: LED `off()` requested (fail-safe), state `FAULT`, `faults++`; `reset()` returns to `IDLE`.
- Completed samples are pushed to a caller-owned `nn2_core_ring_t`; ring-full is drop-and-count, not a fault.
- Single-context; no synchronization (RTOS review item Q-4).

Status: DRAFT (M1). Host tests: `test/host`.
