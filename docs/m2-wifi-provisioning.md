# M2 Wi-Fi provisioning (issue #30)

USB Serial/JTAG console, one shot, writes an application NVS namespace, then the station reconnects with a bounded backoff. This note is the implementation record for that path. It is not a Class H result and it does not authorize flashing.

## Flow

1. `nn2_prov` (core 0) loads the `nn2_wifi` namespace (`ssid`, `pass`).
2. Missing, invalid, or corrupt records read SSID, then a masked password, from the USB Serial/JTAG console. Each line wait is finite (`NN2_WIFI_PROV_TIMEOUT_MS`). A failed attempt waits `NN2_WIFI_PROV_RETRY_MS` and tries again. There is no SoftAP and no BLE fallback.
3. Valid input is committed from `nn2_prov` only. The password is not logged.
4. `nn2_net` (core 0) connects as a station. The driver stores the config in RAM (`WIFI_STORAGE_RAM`), not in the Wi-Fi internal NVS namespace.
5. Wi-Fi and IP callbacks only enqueue. `IP_EVENT_STA_GOT_IP` posts `LINK_UP` and nothing else. The timer callback only posts `RECONNECT_DUE`. `nn2_net` is the only writer of station status, and the only caller of `nn2_wifi_backoff_reset` and `esp_timer_stop`. A queue overflow increments a C11 atomic counter.
6. On each 1 s receive timeout, if the link is down, a connection is still wanted, and the backoff timer is not armed, `nn2_net` schedules a reconnect. That covers a dropped `LINK_DOWN` or `RECONNECT_DUE`. If `esp_wifi_connect` fails inside `reconnect_now`, `nn2_net` schedules the next backoff step.
7. Link up and link down are visible through `nn2_port_wifi_station` (`get_status` and `wait_event`). `get_status` samples the driver from `nn2_net`, so a dropped event is still visible. That report is the hook a later issue can use.

Reconnect delays, from `NN2_WIFI_BACKOFF_FIRST_MS` and `NN2_WIFI_BACKOFF_MAX_MS` (Kconfig on target, the same defaults in `nn2_wifi_limits.h` for host tests): 0 ms, then 500 ms, doubling, capped at 8000 ms. `FIRST_MS` is asserted `<= 1000` and `< 3000`. ADR-0003 optical numeric cells stay OPEN. The 3 s grace timer is not this backoff.

## Tasks

| `pcName` | Core | Priority macro | Stack macro | Role |
|---|---|---|---|---|
| `nn2_net` | `NN2_RTOS_CORE_NET` (0) | `NN2_RTOS_PRIO_NET` | `NN2_RTOS_STACK_NET_BYTES` (3072 B) | Station events and reconnect |
| `nn2_prov` | `NN2_RTOS_CORE_PROV` (0) | `NN2_RTOS_PRIO_PROV` | `NN2_RTOS_STACK_PROV_BYTES` (2560 B) | One-shot console and NVS commit |

Both macros live in `components/nn2_rtos/include/nn2_rtos_config.h`. ESP-IDF passes those depths to `xTaskCreateStaticPinnedToCore` as bytes. The static buffers are `StackType_t` arrays of the same count, because this v6.1 kernel still multiplies the depth by `sizeof(StackType_t)` when it fills the stack. The `nn2_net` stack high-water mark is NOT RUN (Class H). The tasks are not watchdog subscribers. Their waits use finite timeouts. `nn2_prov` deletes itself after a successful handoff.

Core 0 system tasks:

| Task | Setting | Evidence |
|---|---|---|
| `wifi` | `CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0` | sdkconfig, build `#error` in `nn2_rtos_config.h` |
| `tiT` (lwIP TCP/IP) | `CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0` | sdkconfig, same build check |
| `esp_timer` | `CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0` | sdkconfig, same build check |
| `sys_evt` | no Kconfig in ESP-IDF v6.1 | `esp_event_loop_create_default` sets `task_core_id` to 0 (`components/esp_event/default_event_loop.c`, IDF `fff9895c`) |

No task in this change is pinned to core 1. Emitter, ADC, and optical code are not created.

## Ports

| Header | Type | Adapter |
|---|---|---|
| `nn2_port_wifi_station.h` | `nn2_port_wifi_station_t` | `nn2_ad_wifi` |
| `nn2_port_wifi_cred_store.h` | `nn2_port_wifi_cred_store_t` | `nn2_ad_cred` |
| `nn2_port_prov_console.h` | `nn2_port_prov_console_t` | `nn2_ad_prov` |

`nn2_core` and `nn2_app` do not include ESP-IDF or FreeRTOS headers. `app_main` is the composition root (`main/nn2_wifi_bringup.c`).

## Credential bounds

SSID length 1..32 octets. WPA2-PSK password length 8..64 octets. Empty SSID and a password shorter than 8 are rejected and are not saved. Control characters are rejected. The console echoes the SSID and masks the password with `*`.

NVS init failure `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND` returns `NN2_ERR_NVS_CORRUPT` and does not erase the partition.

## Out of scope / follow-ups

- Owner lock O6 on #33 (comment 6226931941): prolonged Wi-Fi loss leading to auto `stop_stream` and LED off after a grace period is not implemented. The STA adapter only reports link up/down through `nn2_port_wifi_station` so a later issue can implement O6.
- Owner decision D1 on #33 (comment 6227092516): a 3 s grace period after Wi-Fi loss, then STOPPED, is not implemented. The reconnect sequence is only shaped so the immediate attempt and the 500 ms attempt both fall inside that window.
- Owner lock O8 on #33 (comment 6226931941): the USB console does not trigger emitter shutdown. The console exposes no LED-on or emitter command.
- SoftAP, BLE, and host-API commands over USB (#33 L1) are not implemented.
- Class H on-target join, NVS survival across reboot, optical coexistence, and the `nn2_net` stack high-water mark are NOT RUN. No flash is authorized.

## Evidence

| Class | What it can show |
|---|---|
| U | Host tests of validation, the fake store, backoff, link-loss policy, a dropped link event, and a failed `reconnect_now`. Command: `bash test/host/run.sh` |
| S | `bash tools/class-s-gate.sh .` on `nn2_core`, `nn2_ports`, and `nn2_app` |
| B | `idf.py set-target esp32s3 && idf.py build` (CI job `class-b-esp-idf-build`) |
| H | NOT RUN |

A successful build does not establish electrical behaviour, timing, or a successful association.
