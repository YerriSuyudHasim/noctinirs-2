# Fault and Recovery Table

| Condition | Classification | Immediate action | Escalation/recovery |
|---|---|---|---|
| Single under-K slot | Recoverable quality event | Mark slot partial/invalid per policy | Count; next slot continues |
| Single SPI failure | Recoverable initially | Reject sample/slot contribution | 3 consecutive failures stop/fault session |
| Single CRC failure | Recoverable initially | Break contiguous-K tail; count CRC | 3 consecutive failures stop/fault session |
| Single ADS status error | Recoverable initially | Reject sample and count | 3 consecutive errors stop/fault session |
| DRDY period gap | Recoverable initially | Count integrity gap | 4 consecutive gaps stop/fault session |
| DRDY silence > 5000 us | Fatal session fault | Stop acquisition path | `ADC_DRDY_STOPPED` |
| DRDY event-ring overflow | Fatal integrity fault | Stop publication/session | `DRDY_EVENT_OVERFLOW` |
| Raw ring overflow | Fatal integrity fault | No overwrite; stop session | `ADC_RING_OVERFLOW` |
| Single I2C NACK/submit error | Recoverable hardware fault | Invalidate slot, request force OFF | Dedicated recovery task |
| I2C timeout | Recoverable once, safety-critical | Invalidate slot, force RESET#/OFF | Reinit/readback; repeated threshold faults |
| I2C completion-ring overflow | Lifecycle/integrity fault | Reject completion; safe recovery | Escalate if repeated |
| Stale I2C callback | Rejected lifecycle event | Do not apply to current slot/session | 3 repeated violations become fatal |
| Scheduler miss | Recoverable once | Invalidate slot/frame | 3 repeated misses fault session |
| Slot timing ring overflow | Fatal frame integrity fault | Invalidate affected frame/session | `OPTICAL_PROCESSING`/reason code |
| Completed-frame ring full | Recoverable stream pressure | Drop network frame, no producer overwrite | Count overflow/network drop; acquisition continues |
| Peer absent/network unavailable | Recoverable transport state | Consume/drop completed network frame | Increment `network_drop`; no RT impact |
| UDP send failure/`EAGAIN` | Recoverable transport error | Drop current network frame | Increment `stream_errors`/network drop |
| LED cannot be verified OFF | Fatal safety fault | Hold RESET# asserted | `LED_UNSAFE`/`LED_RECOVERY`; do not resume |
| Network task partial-init failure | Startup failure | Signal stop; bounded task acknowledgements; stop Wi-Fi | Return init error; acquisition remains independently safe |

## Recovery invariants

- Normal slot execution never performs RESET/readback delays.
- The recovery task is persistent and fault-only.
- Session/frame/slot/command identities are checked before completion application.
- Resume never occurs in the middle of a frame.
- Hardware RESET# remains the ultimate PCA9956B fail-safe.
- No callback-owned object is deleted per session.
