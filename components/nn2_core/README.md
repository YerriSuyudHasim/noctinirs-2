# nn2_core (Core layer)

Pure C11 domain types and algorithms (ADR-0001 3.1). No ESP-IDF/FreeRTOS includes; `REQUIRES` nothing.

- `nn2_core_types.h`: `nn2_status_t`, `nn2_core_sample_t`, version macros (`0.1.0`).
- `nn2_core_ring.h`: fixed-capacity (16) sample ring, caller-owned storage, no heap, drop-and-count when full. Not thread-safe (RTOS review item).

Status: DRAFT (M1). Host tests: `test/host`.
