# NoctiNIRSv3 Migration Map — Final

| v16 area/file | Decision | v3 destination | Final result |
|---|---|---|---|
| `app_main_fnirs_test.c` | rewrite | `main/app_main.c` | startup coordinator only; persistent modules/tasks |
| board pins/addresses | port with modification | `nocti_v3_board` | retained; GPIO35–37 remains hardware gate |
| old I2C HAL | rewrite | board + LED | new-driver-only, callback-driven runtime, persistent buffers |
| PCA9956B register logic | port with modification | `nocti_v3_led` | change-on-STOP, prebuilt one-hot/all-OFF, recovery task |
| `nocti_timebase` | rewrite | `nocti_v3_timebase` | one 64-bit 1 MHz common clock and absolute deadlines |
| `nocti_scheduler` | rewrite | `nocti_v3_optical` | explicit epoch/state/deadline machine |
| ADS HAL/driver | port with modification | `nocti_v3_adc` | one 18-byte SPI transaction/DRDY, CRC/status/rate checks |
| TI `hal.c/.h` | reference only | none | MCU-specific waits/HAL not copied |
| TI `ads131m0x.c/.h` | reference only | private protocol layer | register/frame/CRC behavior retained |
| raw modulo history | rewrite | raw SPSC ring | explicit head/tail, no silent overwrite |
| v16 collect/average | port with fixes | optical processing | half-open windows, newest contiguous K mean |
| ON/OFF phase data | port with modification | slot result/frame | explicit `I_LED_OFF`, `I_corrected`, quality masks |
| source/wavelength order | port unchanged in meaning | 20 descriptors | ten 735/850 pairs |
| frame ownership | rewrite | completed-frame SPSC | copied ownership, session/config filtering |
| text commands | port with modification | `nocti_v3_stream` | final compatible command surface and clear errors |
| stream-v1 envelope | port unchanged | stream compatibility header | 12-byte envelope and record IDs frozen |
| fNIRS binary payloads | port unchanged/versioned | stream + Wi-Fi | SESSION v1, CFG v2, paired FRAME v2 records |
| `nocti_wifi` | rewrite | `nocti_v3_wifi` | separate command/stream tasks and drop policy |
| online/offline bit | rewrite | health/network fields | distinct link/IP/peer/acquisition/stream/fault states |
| MBLL/SSR/baseline/bandpass/UI | deferred/excluded | host software | unchanged |
| BLE | exclude | none | disabled |
| ADS1299 mini-PSG | deferred | future project phase | not included |
| old generated build/sdkconfig | exclude | clean build | final defaults supplied; generated sdkconfig must be reviewed |

## Final interface additions

- External completed-frame consumer binding/copy-pop/flush.
- Stream-v1 encoder and exact compatibility types.
- Network result and stream error counters.
- Current-profile stopped-only API that rebuilds persistent payloads.
- Separate system/network health dimensions and UDP/health task telemetry.
- Host protocol, control, capture, and acceptance scripts.
