# Legacy adoption for Communication Foundation

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

Reference manifest: 319 files verified before implementation. No reference files
are edited or build inputs. Code is newly implemented around existing nn2_* policy
and adapters; this is not a wholesale legacy copy or wire compatibility layer.

| Historical path and symbol | Observed behavior | Decision and reason | New location / check |
|---|---|---|---|
| v3/.../nocti_v3_wifi.c: control_task_entry, set_peer, peer_snapshot | ASCII control, shared peer address, separate stream/control tasks | Change: binary validated transactions, endpoint includes port, one poll owner | nn2_app_comm.c; C transactions/lease tests |
| v3/.../nocti_v3_wifi.c: request_stream_reset, reset_stream_state | Stream-generation/reset handshake | Defer streaming; no extra task or queue in 0.1.0 | No stream producer; scope inspection |
| v3/.../nocti_v3_wifi.c: initialize_nvs, start_softap | NVS setup and SoftAP lifecycle | Change to namespace-owned configuration and provisioned STA, no blanket erase | nn2_config, nn2_wifi; storage error source test, board persistence pending |
| v4/.../nocti_v4_network.c: process_control, command_is | Bounded command loop; stream-only HELLO/START/STOP replies | Keep bounded per-poll work, reject ASCII compatibility; add registry and errors | nn2_net, nn2_app_comm; malformed/registry tests |
| v4/.../nocti_v4_network.c: initialize_softap | Hardcoded AP defaults and broad NVS recovery erase | Reject credentials and erase behavior | nn2_config; no secret literals or nvs_flash_erase |
| v4/.../nocti_v4_network.c: crc16_ccitt_false, build_record | CRC16; packed stream headers and optical payloads | Keep standard CRC algorithm concept, use explicit byte encoding; defer frames | nn2_core_protocol; golden vectors and CRC check |
| v4/.../nocti_v4_network.c: peer_destination, network_task | Peer destination, bounded ring draining, heap reporting | Change to peer lease/cache and socket recovery; keep observable counters | nn2_app_comm, nn2_net, USB STATUS |
| v4/.../nocti_v4_board.c: nocti_v4_board_early_safe_init, assert_pca_reset | Assert reset before driver activity | Keep safety ordering, re-derive GPIO18 from current schematic | nn2_board; physical reset procedure pending |
| v4/.../nocti_v4_led.c: fail_safe_reset, nocti_v4_led_reset_hold | Reset fallback and distinction between requested/verified off | Keep explicit software/physical distinction; never release reset in this profile | nn2_board safety port; injected failure C test |
| v4/.../nocti_v4_led.c: program_sync_golden_path, LED/IREF functions | Configures current/channel/register order | Defer to 0.1.1, do not copy electrical constants | No active LED library in 0.1.0 |
| v4 network stream frame structs and timebase calls | Nominal/completed timestamps, session and sequence fields | Keep reboot/time semantics explicit, synthetic sequence accounting only | GET_TIME, Python SequenceTracker; no DRDY claim |

`...` in the table expands to `components/<component-name>/` below reference/v3
or reference/v4. Actual library headers contain platform dependencies; those are
not brought into the new pure C policy. Existing active ADC/sequencer components
are retained but not invoked by the communication profile.

Conflict: local ROADMAP mentions Proposed ADR-0004 but its source is absent. The
new NN2-CF1 specification is isolated and documented; no unseen remote format is
silently accepted. GPIO/reset facts come from local schematic endpoints, not
historical constants. Actual assembly and emitter-off effectiveness remain untested.
