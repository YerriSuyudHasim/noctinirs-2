# Prompt 3 State/Event Diagram

```text
SLOT_IDLE
  | absolute slot start / submit one-hot ON
  v
LED_ON_PENDING
  | matching async completion
  v
LED_ON_COMMITTED -> LED_ON_SETTLING
  | absolute actual stable-start
  v
LED_ON_COLLECT
  | absolute nominal LED-OFF / submit all-OFF
  v
LED_OFF_PENDING
  | matching async completion
  v
LED_OFF_COMMITTED -> LED_OFF_SETTLING
  | absolute actual stable-start
  v
LED_OFF_COLLECT
  | absolute guard-start
  v
SLOT_END_GUARD
  | absolute nominal slot-end
  v
SLOT_FINALIZE
  | derive next nominal timestamp from frame origin
  +-----------------------------------------------> next LED_ON_PENDING
```

Fault transitions from every state:

```text
invalid epoch/command, NACK, timeout, queue/ring overflow,
missed deadline, late/empty stable window
  -> SLOT_INVALID / LED_RECOVERY
  -> RECOVERING
  -> FAULTED with PCA RESET# asserted during recovery entry
```

STOP transition:

```text
RUNNING_SCAN
  -> invalidate generation + disarm alarm + queue FORCE_OFF
  -> ignore in-flight ON as a normal transition
  -> matching successful all-OFF completion
  -> stop/drain ADC
  -> STOPPED
```
