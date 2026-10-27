> SUPERSEDED WIRE CONTRACT (2026-10-27): active source now uses ADR-0004/1.
> See [the current local implementation binding](adr/0004-local-implementation.md).
> CF1 packets, CLAIM/RELEASE and the examples below are historical and are not
> accepted by the current firmware/client. SoftAP-only remains the network mode.

# v0.1.0 function and component map

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

> Current implementation: USB provisioning + STA. The approved SoftAP/STA revision
> is specified in NETWORK_ONBOARDING.md and is not implemented yet. Existing
> NN2-CF1 operational packets remain unchanged.

The application is a communication endpoint with real USB, NVS, STA and UDP
adapters. It is not an optical acquisition application. Hardware-dependent
command outcomes carry the SIMULATED flag; GPIO18 RESET_LED stays asserted.

| Startup/poll action | Public API | Owner |
|---|---|---|
| Establish emitter-inactive request first | nn2_board_init_safe | nn2_board |
| Reassert reset on faults/stop/each loop | nn2_board_hold_emitters_off | nn2_board |
| Inspect software reset state/reason | nn2_board_get_boot_info | nn2_board |
| Open NVS without broad erase | nn2_config_init | nn2_config |
| Read/store validated STA credentials | nn2_config_load_network / save_network | nn2_config |
| Allocate persistent reboot identity | nn2_config_next_boot_id | nn2_config |
| Expose storage/off ports | nn2_config_storage_port / nn2_board_safety_port | Platform components |
| Install USB RX and process bounded input | nn2_provision_init / poll | nn2_provision |
| Validate WIFI hex command independently | nn2_app_provision | Existing nn2_app |
| Validate network config values | nn2_app_network_config_valid | Existing nn2_app |
| Initialize STA resources, start with credentials | nn2_wifi_init / nn2_wifi_start | nn2_wifi |
| Advance reconnect state, obtain link snapshot | nn2_wifi_get_status | nn2_wifi |
| Stop Wi-Fi on fatal failure | nn2_wifi_stop | nn2_wifi |
| Initialize and advance application policy | nn2_app_comm_init / tick | Existing nn2_app |
| Execute request, deduplicate and reply | nn2_app_comm_process | Existing nn2_app |
| Encode/decode/check CRC | nn2_core_packet_encode / decode / crc16 | Existing nn2_core |
| Open/recover UDP, receive/send bounded batch | nn2_net_init / poll / stop | nn2_net |
| Obtain transport and provisioning counters | nn2_net_get_status / nn2_provision_get_status | Platform components |
| Obtain monotonic time | Existing nn2_adapter_clock_esp_port | main/adapters |

## Ownership and dependencies

`app_main` injects the board off port, clock and NVS storage port and owns all poll
calls. Only SDK Wi-Fi/event tasks run concurrently; their callbacks publish a
small snapshot under a FreeRTOS critical section, without socket or NVS work.
There are no new application tasks, ISRs, sample queues or per-component tasks.

- nn2_core: byte codec and wire value constants, C11 only.
- nn2_ports: existing clock interfaces plus safety and network storage contracts.
- nn2_app: command policy/cache and provisioning validation, C11 only; uses ports.
- nn2_board: ESP GPIO/system. No LED-on API; no LED bus initialization.
- nn2_config: NVS and application validation. One versioned credential blob.
- nn2_provision: USB Serial/JTAG and application parser through the injected store.
- nn2_wifi: ESP Wi-Fi/event/netif, validated network values; no protocol policy.
- nn2_net: lwIP sockets, app request processing and injected monotonic clock.

Only nn2_core/nn2_ports/nn2_app are inner layers. New platform components are
explicitly inventoried in tools/platform_components.json and forbidden as inner
includes/dependencies. Unknown nn2_ directories still fail detection. Existing
fake/diagnostic adapters are retained.
The old monolithic communication draft is removed after integration.

## Bounded behavior

UDP: maximum 142-byte packet, 128-byte payload, 4 datagrams/poll, no application
queue. Oversized/truncated datagrams are rejected. Nonblocking socket IO; reopen
no faster than once/second. TX-buffer exhaustion counts a dropped reply; host
retries reuse request ID and the original endpoint. Kernel RX overflow is visible
as host loss/timeouts, not an exact device drop count in this release.

Application: eight request+response cache entries in caller-owned 4096-byte opaque
storage. No heap. Operations copy bounded private state onto the owner stack.
Communication defaults reserve 12288 bytes for main; actual watermark is NOT RUN.
Policy uses a 3-second lease; duplicate responses never extend the lease.

USB: 64 bytes/poll, 196-byte maximum line, 10-second partial-line deadline,
512-byte RX/TX driver buffers. Input is never echoed. Overflow drains until LF;
partial credentials are erased. NVS operations are synchronous SDK calls and may
delay a poll; there is no measured hard real-time guarantee. At 100 Hz RTOS tick,
the main loop yields one tick (nominally 10 ms); actual scheduling is unmeasured.

STA: one connection attempt at a time, 15-second attempt deadline, retry backoff
1/2/4/8/16/32 seconds capped at 32. Successful IPv4 acquisition resets backoff.
Link epoch changes revoke peer state even if loss and recovery occur between polls.

## Failure behavior

Initialization failure is logged by stage, stops network activity and reasserts
reset. Invalid/missing network credentials leave USB provisioning available.
Stored replacements apply after reboot when Wi-Fi has already started; a first
provisioning starts STA without reboot. A storage error reports unknown persistence
outcome and never reports successful application. No automatic NVS erase.

Software off failures become fault 255; backward monotonic time becomes fault 254
and requires reboot. No command can release emitter reset or start ADC capture.
