# NoctiNIRSv3 Health Counter Semantics

Counters are monotonically increasing within a reset interval. `RESET_COUNTERS`
is accepted only while fully stopped and does not reset lifecycle state,
`session_epoch`, configuration identity, DRDY sequence identity required by the
next START barrier, online-state timestamps, or persistent fault identity.

## System/network health

| Field | Exact meaning |
|---|---|
| `boot_count` | Firmware-instance boot counter initialized to 1 for this runtime |
| `command_count` | Parsed command requests |
| `command_errors` | Supported commands that returned failure plus malformed requests |
| `unsupported_commands` | Unknown/deprecated command requests |
| `udp_rx_packets` | Nonempty UDP control datagrams received |
| `udp_tx_packets` | Successful command replies plus successful binary stream records |
| `udp_socket_errors` | Failed control reply or stream record sends |
| `wifi_ap_started` | ESP-IDF reported SoftAP start and not stop |
| `wifi_station_count` | Currently associated SoftAP stations as observed from events |
| `network_ready` | AP/IP networking is available; independent of peer and acquisition |
| `udp_peer_active` | A command sender has configured the stream destination; silence alone does not clear it |
| `stream_enabled` | START completed and UDP frame transmission is enabled |
| `last_command_receive_us` | Common-timebase timestamp of last command datagram |
| `last_successful_transmit_us` | Timestamp of last successful binary stream record send |
| `udp_peer_ip4` | Host-order IPv4 address of configured peer |
| `sensor_fault` | Current acquisition/optical safety fault state |
| `startup_errors` | Startup/system fault-record calls; preserved by runtime counter reset |
| `last_fault`, `last_esp_error` | Most recently recorded system fault and ESP error; preserved |
| task core/watermark fields | Actual runtime core and minimum remaining stack in bytes reported by each application task |

## ADC health

| Field | Exact meaning |
|---|---|
| `adc_ok` | ADS frames with successful SPI, valid CRC/status, and accepted continuity |
| `adc_missed` | Acquisition events known not to produce a valid raw frame, including integrity-overflow paths |
| `adc_sequence_gap` | Missing/nonconsecutive DRDY sequence events |
| `drdy_event_overflow` | DRDY ISR could not publish a compact event or timestamp |
| `adc_ring_overflow` | ADS task found raw ring full; no overwrite occurred |
| `adc_period_gap` | DRDY period exceeded the configured gap threshold |
| `adc_period_min_us`, `adc_period_max_us` | Observed minimum/maximum DRDY period |
| `adc_max_gap_us` | Largest observed DRDY gap |
| `crc_bad` | ADS output CRC mismatch count |
| `ads_status_errors` | ADS status/response failed configured validation |
| `spi_errors` | SPI transaction API failures |
| `spi_duration_max_us` | Maximum measured SPI transaction duration |
| consecutive error fields | Current consecutive run used by fatal thresholds |
| ring depth fields | Current/maximum event and raw ring pressure |
| `measured_sps` | Startup measured DRDY rate |
| `measured_period_average_us_x1000` | Startup average period in fixed-point microseconds ×1000 |
| `inferred_clkin_hz` | CLKIN inferred from measured SPS and configured OSR |
| `session_integrity_fault` | Sticky current-session integrity failure |

## LED/I2C health

| Field | Exact meaning |
|---|---|
| `led_errors` | LED command/completion/recovery errors counted by LED subsystem |
| `i2c_nack` | Runtime completion/submit classified as NACK |
| `i2c_timeout` | Runtime LED command timeout |
| `i2c_queue_full` | Async submit could not be queued |
| `i2c_completion_ring_overflow` | Callback could not publish completion without overwrite |
| `stale_i2c_completion` | Completion identity did not match current session/frame/slot/command |
| `recovery_count` | Dedicated LED recovery attempts |
| `hardware_reset_count` | PCA RESET# hardware reset cycles |
| `led_commit_latency_max_us` | Max request-to-completion conservative commit latency |
| `consecutive_errors` | Current LED error run used by recovery threshold |
| `safe_off_count` | Verified/commanded fail-safe OFF operations |
| current fields | Active next-start 735/850 current profiles |
| task/core fields | Actual LED/recovery task and I2C init/callback cores plus stack watermarks |

## Optical/frame health

| Field | Exact meaning |
|---|---|
| `frames_total` | Frames finalized by optical processing |
| `frames_valid` | Frames with all 20 slots target/minimum-valid and all 10 pairs present |
| `partial_frames` | Frames structurally complete but containing at least one partial-quality slot |
| `invalid_frames` | Missing/invalid/timing-corrupt/truncated frames |
| `slots_total` | Slot results finalized |
| `underk_on`, `underk_off` | Finalized slots with used count below K minimum 8 |
| `subtarget_on`, `subtarget_off` | Slots with 8–9 samples, valid-minimum but below target 10 |
| `min_n_on`, `min_n_off` | Minimum used sample count observed among finalized slots after reset |
| `raw_stable_on` | Raw samples classified into stable LED-ON interval |
| `raw_stable_i_led_off` | Raw samples classified into stable `I_LED_OFF` interval |
| reject transition | Samples during ON/OFF command transition |
| reject settle | Samples during commit-derived settling |
| reject guard | Samples at/after guard start and before slot end |
| reject stale epoch | Samples with wrong session/config/slot identity |
| reject ambiguous | Samples lacking an unambiguous immutable timing record/boundary |
| `sched_missed` | Scheduler deadline missed beyond allowed state transition |
| `sched_abort` | Scheduler aborted active slot/frame/session |
| `scheduler_event_overflow` | GPTimer/deadline event could not be consumed without loss |
| `scheduler_lateness_max_us` | Maximum callback/event processing lateness versus nominal alarm |
| `led_on_stable_min_us`, `led_off_stable_min_us` | Minimum commit-derived stable window observed |
| `slot_timing_ring_overflow` | Scheduler could not publish timing record without overwrite |
| `sample_scratch_overflow` | Per-slot classification staging exceeded static capacity |
| `completed_frame_ring_overflow` | Completed-frame producer found ring full; frame not overwritten |
| `completed_frames_consumed` | Frames removed by bound external/fallback consumer |
| `network_drop` | Completed optical frames discarded from network delivery due no peer, disabled/unready network, ring pressure/flush, or incomplete send |
| `stream_errors` | Stream operation failures such as encoding/send/reset/consumer errors; not necessarily one per datagram |
| `network_frames_sent` | Completed optical frames for which all required frame records succeeded |
| `network_records_sent` | Successful SESSION/TIME64/CFG/FNIRS_FRAME records attributed to frame delivery |
| completed-frame depth fields | Current and maximum ring pressure |

## Interpretation notes

- `network_drop` does not imply ADC or optical acquisition failure.
- `udp_tx_packets` is a transport-level record/reply count, while
  `network_frames_sent` is an optical-frame delivery count.
- Completion timestamps are conservative software commit boundaries, not direct
  photonic edge measurements.
- A zero watermark before a task has run is not a measured stack margin.
