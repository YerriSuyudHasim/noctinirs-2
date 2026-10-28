# ESP-IDF v6.1 API choices

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

Inspected local SDK: `C:/esp/v6.1/esp-idf`, tools/cmake/version.cmake declares
6.1.0. Git revision query was denied by the environment; exact revision is **not
verified**. EIM metadata identifies a v6.1 installation. No SDK was installed or
replaced. Tool paths available: xtensa-esp-elf esp-15.2.0_20251204, Espressif Clang
21.1.3 esp-21.1.3_20260408, CMake 4.0.3. These are environment inventory, not build
evidence. Compiler actually used, warnings, ELF size and binary size: NOT RUN.

| API / facility | Reason | Local SDK source consulted / limitation |
|---|---|---|
| gpio_set_level + gpio_config | Preload low, then enable output and retain emitter reset | components/esp_driver_gpio/include/driver/gpio.h; no proof before app_main or of physical voltage |
| nvs_flash_init, nvs_get/set_blob, nvs_commit | Versioned credential value and explicit persistence errors | components/nvs_flash/include/nvs.h and nvs_flash.h; no global erase, no encryption enabled by this track |
| nvs_get/set_u64 | Persistent boot counter independent of pre-RF PRNG entropy | Same NVS API; one write/startup, NVS erasure resets identity history |
| esp_netif_create_default_wifi_sta | Supported STA netif and DHCP integration | components/esp_wifi/include/esp_wifi_default.h |
| esp_event_handler_instance_register | Explicit handler lifetime; callback only snapshots state | components/esp_event/include/esp_event.h; no callback socket/NVS work |
| esp_wifi_init/config/start/connect/disconnect | Supported STA lifecycle, no legacy SoftAP password | components/esp_wifi/include/esp_wifi.h; bounded application retry policy, SDK latency unmeasured |
| WIFI_STORAGE_RAM, init.nvs_enable=false | Avoid two independent stores of Wi-Fi credentials | esp_wifi.h/types; nn2_config owns persistence |
| usb_serial_jtag_driver_install/read_bytes/write_bytes | Existing ESP32-S3 USB transport, fixed buffers, no echo | components/esp_driver_usb_serial_jtag/include/driver/usb_serial_jtag.h |
| usb_serial_jtag_vfs_use_driver | Route SDK console and provisioning TX through the same driver | driver/usb_serial_jtag_vfs.h and src/usb_serial_jtag_vfs.c; current API name, finite SDK flush timeout |
| Nonblocking lwIP UDP socket and fcntl | Bounded per-poll batches without a new task per library | components/lwip; send/recv errors are counted and ownership revoked as needed |
| esp_timer_get_time through existing clock port | Monotonic microseconds without platform types in policy | components/esp_timer/include/esp_timer.h; resolution is not capture precision |
| FreeRTOS critical sections and vTaskDelay | Small shared Wi-Fi snapshot, cooperative owner loop | Bundled FreeRTOS; no ISR application code, task watermark unmeasured |

Official references: [v6.1 ESP32-S3 guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/index.html),
[Wi-Fi](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/network/esp_wifi.html),
[ESP Timer](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/esp_timer.html),
[style guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/contribute/style-guide.html).

Configuration input: sdkconfig.defaults + sdkconfig.communication; target ESP32-S3,
main stack 12288, 100 Hz FreeRTOS tick, USB Serial/JTAG console, PSRAM disabled.
The old root sdkconfig/build are not rewritten. No GPTimer, DMA, streaming task,
ADC ISR, secure boot, flash encryption or new third-party firmware package is
introduced for this communication-only slice.
