# Prompt 3 Real-Time Review

## ISR/callback safety

- DRDY ISR: timestamp, sequence, compact ring, overflow flag, notify only.
- GPTimer callback: alarm/count timestamp, event/generation, notify only.
- I2C callback: common timestamp, pending immutable identity, result, compact
  completion ring, counters, notify only.
- No callback logs, blocks, resets, reads back registers, allocates, sends UDP,
  performs SPI acquisition, or assembles frames.
- I2C callback and directly called max/timestamp helpers are IRAM annotated.

## Persistent-buffer lifetime

- All 20 one-hot payloads and all-OFF payload are fields of static LED runtime.
- The LED task does not dequeue a new request while `transaction_pending` is
  true, so callback identity and buffer cannot be overwritten prematurely.
- Callback is deregistered before synchronous init/recovery transactions.

## Event ordering and stale protection

- Scheduler accepts completion only when session epoch and slot epoch match.
- State plus command ID must match the pending ON/OFF command.
- Generation invalidates already delivered timer events at STOP/fault.
- Completion later than the measured command deadline faults even when task
  processing order observes completion before the queued timeout event.
- Any failure to arm a future absolute deadline faults immediately.

## START/STOP review

- All tasks/queues/rings/driver handles exist before START.
- START uses ADC acknowledgement and a future absolute frame origin.
- STOP disarms new deadlines, invalidates generation, requests all-OFF, waits
  for completion/timeout, then quiesces ADC.
- No object used by an ISR/callback is deleted during START/STOP.
- No fixed delay is used as START/STOP acknowledgement.

## Open real-time evidence

- Exact I2C/GPTimer/DRDY interrupt core and priority allocation.
- Linker-map confirmation that all cache-safe callback paths and data are
  internal.
- Measured callback and scheduler latency under flash and Wi-Fi activity.
- Logic-analyzer confirmation that completion-derived boundary is conservative
  relative to the PCA output update and optical response.
