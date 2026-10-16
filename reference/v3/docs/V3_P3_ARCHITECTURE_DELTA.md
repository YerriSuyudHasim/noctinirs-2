# Prompt 3 Architecture Delta

## From Prompt 2 to Prompt 3

```text
P2: PCA RESET# held LOW; no LED runtime; absolute timer API disarmed
P3: verified PCA initialization -> persistent async command path
    -> completion-derived boundaries -> absolute slot state machine
```

### Event path

```text
GPTimer absolute alarm ISR
  -> compact deadline record + scheduler notify
  -> scheduler validates generation/state
  -> enqueue immutable LED request
  -> core-1 LED task selects persistent prebuilt payload
  -> nonblocking I2C submit
  -> I2C transaction-done ISR callback
  -> common timestamp + immutable request identity + result
  -> static completion ring + scheduler/LED task notifications
  -> scheduler validates session_epoch/slot_epoch/command_id
  -> conservative commit/stable boundary
```

### Recovery path

```text
NACK / timeout / queue full / missed deadline / invalid boundary
  -> slot/session invalid
  -> PCA RESET# asserted immediately
  -> ADC and scheduler stopped
  -> recovery task quiesces transaction/queue
  -> callback deregistered
  -> synchronous reset/program/readback
  -> callback registered again
  -> optical session remains faulted/stopped
```

### Explicit design decisions

- No semaphore wait is permitted in the runtime LED submit path.
- Only one PCA device adopts asynchronous operation on the bus.
- Callback completion is a conservative commit marker, not the physical edge.
- Change-on-STOP is used so all six LEDOUT registers update together.
- IREF is configuration state, not a per-slot transaction.
- No automatic resume inside a partially completed frame.
