# Current SoftAP-only v0.1.0

> LED update 2026-10-27: default Fs is now 5 Hz and configured currents are
> 30 mA per optical slot. Default profile still simulates drive. The explicit
> CONFIG_NN2_LED_BRINGUP option adds real LEDs: see [LED_BRINGUP.md](LED_BRINGUP.md).
> A SIMULATED reply alone does not prove emitter power is unavailable in that mode.

Owner decision: the future second board will provide a router. This release uses
ESP32-S3 SoftAP only. This supersedes all earlier STA, AP+STA onboarding, QR,
automatic Windows credential capture and v0.1.0.1 planning in this workspace.

- SSID: `NoctiNIRS`; password: `123456789` (Owner-selected public prototype value).
- AP IPv4/gateway: `192.168.4.1`, mask `255.255.255.0`; SDK DHCP server assigns host IP.
- UDP control: port 3333 by default; ADR-0004/1 envelope and control binding (see adr/0004-local-implementation.md).
- WPA2/WPA3 Personal transition mode, CCMP, SAE both derivation methods. PMF is
  optional for WPA2 compatibility; WPA3 clients negotiate its required protection.
- 2.4 GHz, channel 1, one associated computer. No router, NAT or internet service.
- Safe boot/off first, NVS boot identity, optional USB diagnostics, AP and UDP.
  USB WIFI/SETUP commands are rejected; USB STATUS and SAFE_OFF remain. Old NVS credentials
  are neither loaded into Wi-Fi nor erased. No secrets are printed in normal logs.
- A station disconnect/AP stop advances link_epoch and arms a 3 s grace.
  Reconnection alone does not cancel it; a valid owner request is required.
  Owner silence is detected after 1.5 s and also starts the 3 s grace. After expiry,
  emitters are requested off and a new start/session is required.
- No claim of unique identity from SSID: every unit has the same requested name.
  For multiple nearby units, a future host must distinguish BSSID/device identity.

## Owner build and first communication check

Use the existing VS Code ESP-IDF v6.1 ESP32-S3 Build action. No build/flash was run
by the assistant. Required SAE options are in defaults and already enabled in the
current root sdkconfig. Main stack remains 12288. Do not erase NVS or user config.

After authorized flashing/board preparation, join NoctiNIRS using the password
above. Keep the connection when Windows reports no internet, then run:

```powershell
python tools/nn2ctl.py info --host 192.168.4.1
python tools/nn2ctl.py status --host 192.168.4.1
python tools/nn2ctl.py get-time --host 192.168.4.1
python tools/nn2ctl.py run --host 192.168.4.1 --seconds 10
```

START remains SIMULATED; no LED/ADC activation or FRAME producer. A STREAMING state is a control
simulation, not proof of data capture. The run command keeps one UDP endpoint and
sends heartbeats every 500 ms. Do not expect a standalone start invocation to
keep a session alive after its client exits. Retain existing soak limits:
30 minutes, 10 client disconnect/reconnect cycles and 10 board restarts. Replace
router interruption with disconnecting/reconnecting the computer's AP association.
Verify DHCP, WPA2/WPA3 interoperability, no stale ownership and physical off behavior.
No Wi-Fi power saving or battery endurance improvement is claimed for AP-only.

## API ownership and exact SDK inputs

nn2_wifi_init/start/get_status/stop own AP resources and lifecycle.
nn2_wifi_bind_socket confines nn2_net to the AP interface using SO_BINDTODEVICE.
The Wi-Fi callback only updates a snapshot under a short FreeRTOS lock. There is
no new application task, polling retry loop, credential endpoint or handover timer.
Main owns UDP polling; failures propagate through its existing off/stop path.

Checked installed v6.1 inputs: esp_wifi.h; esp_wifi_types_generic.h
(wifi_ap_config_t); esp_wifi/Kconfig (SOFTAP_SAE_SUPPORT); esp_netif.h;
lwip/sockets.h. Use esp_netif_create_default_wifi_ap, explicit DHCP/IP setup,
esp_event_handler_instance_register and esp_wifi_set_storage(WIFI_STORAGE_RAM).
Do not use deprecated tcpip_adapter or copy the V3/V4 networking stack wholesale.

Shared prototype password does not authenticate the application or secure ADR-0004/1
against another admitted peer. WPA3 availability is not a production security claim.
Source/static/Python results are separate from build and on-board success.
