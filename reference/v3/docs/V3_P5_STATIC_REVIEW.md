# Prompt 5 Final Static Review

## Verified by host-side tooling

- 11/11 C translation units pass Clang syntax/type/warning checks with
  `-std=gnu11 -Wall -Wextra -Werror -Wformat=2` using narrow ESP-IDF stubs.
- Stream-v1 C compatibility test passes CRC, layout, and sequence checks.
- Python protocol/control/capture/acceptance tools compile.
- No Arduino API or legacy I2C driver include appears in application source.
- No application `malloc`, `calloc`, `realloc`, or `free` call appears; matches
  on `spi_bus_free` are driver cleanup APIs, not heap calls.
- No `tskNO_AFFINITY` is used by application tasks.
- `sendto` appears only in the non-real-time Wi-Fi component.
- No per-sample or normal per-slot logging is present.
- DRDY, GPTimer, and I2C callbacks perform compact event/timestamp work only.
- Async PCA command payloads are persistent static/runtime objects.
- Raw, timing, completion, and completed-frame rings use explicit head/tail and
  release/acquire publication ordering.
- Classification uses half-open intervals and ISR DRDY timestamps.
- Completed frames are copied into UDP-task storage before encoding.
- Network failure follows drop policy rather than blocking/retrying RT tasks.
- START/STOP use epoch invalidation and acknowledgement barriers; per-session
  object deletion is absent.
- Partial network task initialization now follows bounded task-stop
  acknowledgements before Wi-Fi cleanup.

## Protocol delays found and accepted

- ADS hardware reset/synchronization pulse and register recovery delays.
- PCA hardware RESET# low/recovery delays.
- LED recovery cooldown in the dedicated recovery task.
- Five-second delay in the lowest-priority health logger.

No software delay is used as a START/STOP quiescence substitute.

## Not proven by host static checks

- ESP-IDF 5.5.1 API/link compatibility and generated Kconfig symbols.
- IRAM/DRAM placement and cache-disabled behavior.
- Hardware interrupt core allocation.
- Stack margins or WCET.
- Electrical timing, GPIO/module compatibility, CRC correctness on the board,
  optical edge timing, or K distribution.
