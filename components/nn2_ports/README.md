# nn2_ports (Ports layer)

Header-only port contracts (ADR-0001 3.4/3.6): each port is a struct of function pointers plus `void *ctx`. Depends only on `nn2_core` types.

| Header | Port | Notes |
|---|---|---|
| `nn2_port_led.h` | LED/emitter on/off | `off()` must always be safe (fail-safe) |
| `nn2_port_adc.h` | single raw ADC read (pull) | |
| `nn2_port_clock.h` | monotonic µs timebase + optional delay | |
| `nn2_port_log.h` | leveled text log | never from ISR |

Timing figures in headers are expectations to be measured (Class H), not guarantees. Status: DRAFT (M1).
