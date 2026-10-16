# Final Task/Core/Priority Audit

Stack sizes are configured in bytes. Runtime stack-watermark fields are exported
through HEALTH; values below are not claimed as measured because no hardware log
was available in the generation environment.

| Task | Responsibility | Core | Priority | Stack | Blocking API | Producer/consumer | Measured watermark | Expected WCET/status |
|---|---|---:|---:|---:|---|---|---|---|
| `nocti_v3_adc` | DRDY events, 18-byte ADS SPI read, raw record publication | 1 | 20 | 4096 B | task notification, polling SPI | consumes DRDY ring; produces raw ring | Pending hardware | Ideal SPI wire time 18 us; driver/IRQ WCET pending |
| `nocti_v3_scheduler` | absolute slot state machine and deadline handling | 1 | 19 | 4096 B | task notification | consumes GPTimer/I2C events; produces LED requests/timing | Pending | Must remain below measured deadline slack |
| `nocti_v3_led` | submit prebuilt PCA9956B commands | 1 | 18 | 5120 B | queue receive; async submit returns immediately | consumes LED requests; I2C HW producer | Pending | 180 us ideal wire at 400 kHz; 72 us at 1 MHz |
| `nocti_v3_optical` | timestamp classification, K selection, frame assembly | 0 | 12 | 6144 B | task notification/bounded wait | consumes raw/timing; produces completed frames | Pending | Non-ISR; must drain 8 kSPS raw ring without growth |
| `nocti_v3_led_recovery` | fault-only RESET#, reinit, readback, OFF verification | 1 | 10 | 5120 B | semaphore/delays during recovery | consumes recovery request | Pending | Not on normal slot path |
| `nocti_v3_udp_ctrl` | command receive/parse/reply | 0 | 9 | 6144 B | `recvfrom` with 250 ms timeout, reply `sendto` | command endpoint | Pending | Non-real-time |
| internal frame sink | fallback consumer when external consumer absent | 0 | 8 | 6144 B | bounded wait | completed-frame consumer | Pending | Dormant after UDP consumer binding |
| `nocti_v3_udp_stream` | frame copy, packet encode, nonblocking UDP send/drop | 0 | 8 | 6144 B | task notification; nonblocking `sendto` | sole external completed-frame consumer | Pending | Non-real-time; drops instead of blocking |
| `nocti_v3_health` | 5-second snapshots and compact logs | 0 | 5 | 4096 B | `vTaskDelay` | read-only snapshot consumer | Pending | Lowest application priority |
| `app_main` | persistent object initialization only | 0 by sdkconfig | system | ESP-IDF | initialization APIs | bootstrap | Pending | No acquisition loop |

## Priority assertions

The source enforces:

```text
ADC 20 > scheduler 19 > LED 18 > optical 12 > recovery 10
       > command 9 > UDP/frame sink 8 > health 5
```

Equal priority 8 for UDP stream and fallback frame sink is intentional because
only one completed-frame consumer is active after binding.

## Interrupt/callback allocation audit

| Context | Allocation target | Actual reporting | Work allowed |
|---|---|---|---|
| ADS DRDY GPIO ISR | service installed from pinned core-1 ADC task | `gpio_isr_init_core`, `gpio_isr_callback_core` | timestamp, sequence, compact ring write, notify |
| GPTimer callback | timer initialized from core-1 ADC task | timebase callback core/health | copy alarm/count identity, compact event, notify |
| I2C transaction callback | PCA bus/device initialized from core-1 LED task | `i2c_init_core`, `i2c_callback_core` | timestamp, result/identity, completion ring, notify |

No callback logs, performs SPI, reads registers, resets hardware, allocates heap,
or waits on a blocking mutex.

## Remaining validation

- Verify actual callback cores on hardware.
- Record all stack watermarks under DARK, SCAN, Wi-Fi stress, fault recovery, and
  soak tests.
- Measure task runtime/WCET distributions and scheduler slack.
- Confirm Wi-Fi/lwIP system tasks are pinned to core 0 in the generated sdkconfig.
