# NoctiNIRSv3 3.4.1 — Serial diagnostics and SPI bus-acquire fix

## Root causes

1. `nocti_v3_ads131m04_acquire_bus()` converted a 3000 ms timeout to FreeRTOS ticks and passed it to `spi_device_acquire_bus()`. ESP-IDF 5.5.1 only accepts `portMAX_DELAY`, so startup returned `ESP_ERR_INVALID_ARG` and printed `acquire finite time not supported now`.
2. V3 source already contained startup/Wi-Fi/health `ESP_LOGI()` calls, and unlike V2 it did not deliberately quiet them during acquisition. To make diagnostics robust against a stale runtime tag level, project tag levels were explicitly restored to INFO at application startup. The unconditional stdio probe from this patch was removed in 3.4.2.
3. The periodic health line now reports `ap`, `stations`, `net_ready`, `peer`, and `stream`, and Wi-Fi logs the DHCP-assigned event. No per-sample or per-slot logs were added.
4. The image flash-size setting now matches the N16 module (`16MB`) instead of `2MB`.

## Files changed

- `main/app_main.c`
- `components/nocti_v3_adc/nocti_v3_ads131m04.c`
- `components/nocti_v3_adc/nocti_v3_ads131m04.h`
- `components/nocti_v3_adc/nocti_v3_adc.c`
- `components/nocti_v3_wifi/nocti_v3_wifi.c`
- `components/nocti_v3_common/include/nocti_v3_version.h`
- `sdkconfig.defaults`
- `sdkconfig`

## Required build procedure

Use ESP-IDF 5.5.1 and run a clean configuration/build because both source and flash header configuration changed:

```text
idf.py fullclean
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## Expected key log sequence

```text
I (...) nocti_v3_main: NoctiNIRSv3 firmware=3.4.2 phase=P5_SERIAL_LOG_UNQUIET ...
I (...) nocti_v3_main: serial_quiet_policy=0 serial_quiet_active=0 log_level=INFO
I (...) nocti_v3_ads131: SPI ready host=SPI3 mode=1 clock=8000000 Hz frame=18 bytes DMA=off
I (...) nocti_v3_adc: ADS startup rate expected=8000 SPS measured=...
I (...) nocti_v3_wifi: SoftAP started
I (...) nocti_v3_wifi: SoftAP SSID=NoctiNIRS channel=6 IP=192.168.4.1
I (...) nocti_v3_wifi: station connected aid=... stations=1
I (...) nocti_v3_wifi: station DHCP assigned network_ready=1 stations=1
I (...) nocti_v3_main: health ... ap=1 stations=1 net_ready=1 peer=... stream=...
```

The former SPI error must not appear. Hardware validation is still required for ADS response, measured 8 kSPS, CRC/status, and optical timing.
