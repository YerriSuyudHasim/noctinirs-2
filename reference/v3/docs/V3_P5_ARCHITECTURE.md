# NoctiNIRSv3 P5 Final Architecture

## Data path

```text
ESP32-S3 core 1 — real-time domain

ADS131M04 DRDY falling edge
    -> GPIO ISR: common timestamp + sequence + compact event
    -> ADS acquisition task: one 18-byte SPI transaction
    -> raw-sample SPSC ring

GPTimer absolute alarm
    -> compact deadline event
    -> scheduler task
    -> LED-control request
    -> nonblocking PCA9956B I2C transaction
    -> I2C completion callback
    -> compact completion ring
    -> scheduler commit/stable boundaries

ESP32-S3 core 0 — processing/network domain

raw-sample SPSC ring
    -> optical processing task
    -> timestamp classification + latest-contiguous-K mean
    -> 20-slot frame assembly + 735/850 pairing
    -> completed-frame SPSC ring
    -> UDP stream task
    -> stream-v1 datagrams to port 3334
```

## Ownership contract

| Object | Producer/owner | Consumer | Full/slow policy |
|---|---|---|---|
| DRDY event ring | DRDY ISR | ADS task | Count overflow and fault session integrity |
| Raw sample ring | ADS task | Optical processing/DARK diagnostic | No overwrite; overflow faults session |
| LED request queue | Scheduler | LED-control task | Queue-full invalidates slot and triggers recovery |
| I2C completion ring | I2C ISR callback | Scheduler | Overflow counted; stale identity rejected |
| Slot timing ring | Scheduler | Optical processing | Overflow invalidates timing/frame |
| Completed-frame ring | Optical processing | UDP stream task | Frame dropped; `network_drop`/overflow counters increment |
| UDP datagram buffer | UDP stream task | lwIP `sendto` call | Persistent task-owned buffer; no producer pointer is transmitted |

The external UDP stream task is bound as the sole completed-frame consumer. It
copies the complete frame into task-owned persistent storage before encoding and
therefore never holds a pointer to a frame slot returned to the producer.

## Network backpressure

- Stream socket is nonblocking.
- No RT mutex is held during `sendto()`.
- The stream task always drains completed frames.
- Missing peer, disabled stream, unavailable network, `EAGAIN`, partial send, or
  other send failure causes the network representation of that frame to be
  dropped and counted.
- ADC acquisition, scheduler, LED control, and optical processing continue.
- No unbounded retry queue or blocking retransmission is used.

## Online-state dimensions

The device does not use one ambiguous `online` bit. Health exposes:

- `wifi_ap_started`: SoftAP link service started;
- `wifi_station_count`: stations associated with SoftAP;
- `network_ready`: AP/IP network stack ready;
- `udp_peer_active`: a command sender has configured the destination peer;
- `stream_enabled`: acquisition START completed and streaming is allowed;
- lifecycle/mode: acquisition state;
- `last_successful_transmit_us`;
- `last_command_receive_us`;
- `sensor_fault` and `last_fault`.

Command silence alone does not mark the peer offline. The conservative current
policy clears the configured peer when any associated station disconnect event
is received; this should be reviewed if multi-client streaming becomes a future
requirement.

## Session and configuration isolation

- `session_epoch` distinguishes acquisition sessions.
- `frame_index` and `slot_epoch` distinguish frame/slot instances.
- `config_generation` identifies the immutable configuration applied at START.
- START/STOP requests reset the stream consumer before changing acquisition state.
- Frames from an old session/config are flushed or rejected.
- Timing/current changes never modify an active slot.
