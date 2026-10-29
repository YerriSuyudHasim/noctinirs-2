# ADR-0004: Local control implementation and V3/V4 communication improvements

> 2026-10-29 Owner direction: the `firmware/v4-idf61` image takes all command
> control over Wi-Fi UDP `nn2ctl`. Serial command control is removed. Opcodes
> `0x0030` (`health`) and `0x0031` (`timing`) are UDP read-only diagnostics.
> A USB STATUS sentence later in this file is history of the earlier binding.
> It is not a command path on that image. Upstream record:
> [0004-host-protocol.md](0004-host-protocol.md).

> Later local amendment: [ADR-0005](0005-led-bringup.md) changes the boot config
> default to 5 Hz / 30 mA and adds an optional real LED-only port. In that mode
> STARTING/STOPPING are asynchronous, info advertises led=REAL_BRINGUP and
> SIMULATED does not imply emitters are disabled. No ADC/FRAME producer is added.
> The earlier control-only implementation description below remains dated history.

Date: 2026-10-27. Status: implemented local control binding, proposed for review.
This does not mark the upstream ADR Accepted or claim full acquisition conformance.

## Decision, source and scope

The Owner requested adoption of ADR-0004, a V3/V4 comparison, communication
improvements, and single-cell LiPo charging at nominal 512 mA with a 4.20 V cell
limit. The upstream proposal is preserved in [0004-host-protocol.md](0004-host-protocol.md)
from PR #55 commit `222f9c45f82b6014db85583924da00e313e5499c`.

Replace the local CF1 wire format in the production C codec, control application
and Python client. Keep ESP32-S3 SoftAP, UDP port 3333, existing emitter-reset hold,
single polling owner, and the battery monitor's idle/acquisition cadence.
No historical source is copied wholesale or modified. No new dependency, SDK,
device operation, GitHub write or release is introduced.

Acceptance: C/Python agree on the envelope and command binding; corrupted,
oversized, wrong-session and wrong-peer packets cannot execute commands; retries
cannot repeat cached mutations; safe-off bypasses cache/ownership; public queries
cannot steal a session or evict mutations; grace deadlines are polled independently
of incoming packets. Charge current/voltage changes require verified readback
before CE goes low. Verification results are in ADR004_EVIDENCE.md.

Accountable local activity is firmware implementation. Formal role assignment,
Hardware/RTOS specialist review, independent review and merge approval are not
claimed. This local decision is a concrete review target, not final hardware evidence.

## Historical comparison and adoption

| Evidence | V3 | V4 | Local improvement |
|---|---|---|---|
| Control path | Separate control/stream sockets and tasks; command handler can wait for a stream reset | One network owner, ASCII control limited to four packets per pass; hardware acquisition control remains serial-only | One owner and at most four UDP requests per poll, no acquisition-reset wait or transport task added |
| Peer identity | Control RX sets peer by IPv4 before semantic command validation; configured stream destination port | HELLO/STREAM_START can replace the stored IPv4 stream destination, fixed stream port | Validate CRC/envelope/registry before effects; lock the full IPv4+source-port endpoint for a session; INFO/GET_CONFIG/GET_TIME remain public |
| Silence | V3 explicitly treats silence as not offline | V4 separates stream enable from acquisition enable | Explicit owner silence detection and bounded grace; no activity inferred from malformed traffic |
| Encoding | ASCII command/reply plus binary optical records | ASCII stream control and binary versioned records with CRC16 | A single versioned binary envelope for requests, replies and future frame/event types; CRC32, explicit byte order, strict lengths |
| Congestion | Stream health/drop reporting | Nonblocking sends and counted frame drops | Preserve nonblocking TX and bounded RX; consume message sequence on reply preparation, expose send errors, no spin retry; client retries identical transaction bytes |
| Session boundaries | Stream-reset handshake and encoder reset | Session/quality reporting, frame ring accounting | Per-boot unique session IDs, boot identity, stale-session rejection, distinct message sequence domain |

Paths examined: `reference/v3/components/nocti_v3_wifi/nocti_v3_wifi.c` (`set_peer`,
`peer_snapshot`, `control_task_entry`, stream reset/send path) and
`reference/v4/components/nocti_v4_network/nocti_v4_network.c` (`process_control`,
`peer_destination`, `send_frame`, `network_task`, session/drop counters). This is
static evidence. The Owner's successful prior communication is useful history,
not a throughput/latency result for these new binaries.

## Envelope and implemented registry

Use upstream section 3.3 exactly: magic `NNS2`, envelope version 1, message type u8,
protocol version u16=1, payload length u16, flags u16, session u32, message sequence
u32, monotonic microseconds u64. Header offsets: 0,4,5,6,8,10,12,16,20; size 28.
All integers are little endian. CRC32/ISO-HDLC is a four-byte LE suffix over header
and payload; check `123456789` -> `CBF43926`. Payload cap 1440, datagram cap 1472.
No packed C structures or native-memory serialization. CF1 and ASCII are rejected.

REQUEST payload is `req_id:u32, opcode:u16, args`. REPLY is
`req_id:u32, opcode:u16, status:u16, body`. Request ID zero is permitted; it wraps
as a uint32. The client correlates source endpoint, request ID and opcode. Wrong
envelope/magic/framing/CRC/length/type or a request shorter than six payload bytes
is dropped. A CRC-valid, parsable request with a protocol mismatch receives
BAD_FRAME. Host flags bit1 or bits2..15 receive RESERVED_FLAGS before any command
effect, including safe_off. Host bit0 is ignored as specified by the proposal.

| Opcode | CLI name | Argument bytes | Mutation cache |
|---|---|---|---|
| 0001 | heartbeat | opaque host timestamp u64 | bypass |
| 0002 | info | none | bypass |
| 0003 | status | opaque host timestamp u64 | bypass |
| 0004 | get-config | none | bypass |
| 0005 | get-time | opaque host timestamp u64 | bypass |
| 0010 | start | none | yes |
| 0011 | stop | none | yes |
| 0020 | safe-off | none | bypass; always reassert off |
| 0021 | clear-fault | none | yes |
| 0100 | set-fs | u8 in {1,2,4,5,8,10} | yes |
| 0101 | set-led-current | channel:u8 0..19, current_mA:u16 0..50 | yes |
| 0030 | health | none | bypass |
| 0031 | timing | none | bypass |

Numbers above are hexadecimal. set-led-current stores simulation configuration
only and cannot energize an emitter. No CLAIM, RELEASE or injected-FAULT opcode
is retained from CF1. start acquires the session's peer lock; stop/safe-off/grace
expiry release it. SAFE_OFF is latched until clear-fault succeeds; it does not
pretend a safety callback succeeded when the callback failed.

## Concrete local reply-body binding

The proposal left several body encodings TBD. This control binding uses a common
20-byte body prefix: `state:u8, fault_code:u16, simulated:u8, boot_id:u64,
t_rx_us:u64`. `simulated` must match header bit0. Error replies have this prefix
only. State numbers and u16 status codes follow upstream sections 3.7/3.13.

Success extensions after that prefix:

- info: ASCII capability text, explicitly `frames=unsupported;capture=unsupported`.
- get-config: `fs:u8, slots:u8=20, current_mA[20]:u16` (42 bytes).
- heartbeat/get-time: `t_host:u64, t_rx_us:u64, t_tx_us:u64` (24 bytes).
- status: the same 24-byte time echo, then `requests, duplicates, grace_expirations,
  malformed, reserved_flags_rejects` as five u32 values and `grace_active:u8`.
- health: ASCII `v4health=1` counters, including `boot_stage`. Not an ACQ1 frame.
- timing: ASCII `v4timing=1` service and SPI bins. Not an ACQ1 frame.
- Other implemented commands: no extension.

All current replies carry SIMULATED. Unsupported subsystem evidence/counters are
omitted, not filled with misleading measurements. Real battery telemetry remains
available via cached USB STATUS, independent of command-generated simulation.
t_rx is sampled in the receive processing path. t_tx is stamped just before
sendto for uncached timed queries, and their CRC is recomputed. It is software
preparation time, not hardware TX time. Cached mutation replies keep original
bytes/timestamps; retransmissions are not fresh clock measurements.

## Explicit refinements to the proposal

1. Four mutation-cache entries compare full opcode, arguments and supplied session,
   not opcode alone. Same ID with different args returns INVALID_ARG without
   changing the original entry. Queries/heartbeats do not evict these entries.
   After framing/CRC/version/argument validation, an exact cached acknowledgement
   is returned before validating the current session. This permits a lost STOP
   reply to be retried after teardown. It never reexecutes an old effect or
   refreshes a different session's liveness; fresh stale-session requests fail.
2. Mutations additionally use wrap-safe increasing request IDs with per-endpoint
   high-water marks, covering the last four mutating endpoints. This rejects stale
   mutations after cache eviction for those endpoints. It intentionally strengthens
   the proposal's equality-only replay rule; this is not authentication, and history
   is bounded (a fifth endpoint can evict old idle history). The paired client uses
   one outstanding transaction and increasing IDs.
3. The platform supplies an RNG seed; IDs advance from that seed, skip zero and
   never repeat within a boot. Exhaustion refuses start. Identity is the pair
   (persistent boot_id, session_id), not a cryptographic authorization token.
4. Heartbeat period selected for this control profile: 500 ms. Owner silence:
   1500 ms. Grace: 3000 ms from link-down observation or the silence deadline.
   Valid successful owner requests, including duplicate acknowledgements, refresh
   activity. Unrelated peers/malformed requests do not. Reconnect alone does not
   cancel grace; a valid owner request is required before expiry. These are selected
   software intervals, not measured RTOS deadlines. No timer callback or task is
   added: absolute monotonic deadlines eliminate stale timer-notification races.
5. No optical producer exists. start/stop are explicitly SIMULATED control-session
   transitions (STOPPED -> STREAMING -> STOPPED); there is no claim that a first
   physical slot completed. Real STARTING/STOPPING completion and RECOVERING need
   a future acquisition port and tests. No FRAME or SESSION_REPORT is emitted.
6. USB `SAFE_OFF\n` invokes the same application's safety/state owner and does not
   refresh UDP liveness. USB STATUS remains cached. Boot-time USB intake before
   app initialization and boot-tag recovery are not implemented.

## Memory, transport and remaining work

Bound cache replies to 256 bytes and cached argument keys to three bytes (all
implemented cached commands fit). Do not allocate four full-MTU request/reply
copies. The monitor/control state uses caller-owned 4096-byte opaque storage,
copied with memcpy, without a special host alignment assumption. The MTU-sized
codec and UDP buffers still use stack; 12288 bytes is the current main task stack
configuration, not proof of an adequate target watermark. Measure before hardware
acceptance. No dynamic allocation or framework include is introduced in policy.

Before real acquisition: implement FRAME/SESSION_REPORT payloads and producers,
independent message/frame/sample loss accounting, real ADC timestamp provenance,
nonblocking optical completion, fault history/boot tags, real subsystem status,
and reviewed scheduling/RTOS evidence. Keep all read-side I2C access outside LED
bus ownership. Safe boot #60 is not integrated by this communication change;
pre-app_main reset behavior and physical protection remain unverified.

## Charging selection

Owner: single-cell LiPo; 512 mA nominal is acceptable; cell maximum 4.20 V.
REG02 ICHG=0 and FORCE_20PCT=0 select 512 mA, preserving BCOLD. REG04 VREG=42
selects 3504+42*16=4176 mV, preserving BATLOWV and VRECHG. A closer 4192 mV
nominal could exceed 4200 mV under positive regulation tolerance. The 4176 mV
selection gives headroom (4176*1.005=4196.88 mV using the datasheet's stated 0.5%
accuracy characteristic), but is not a measured absolute-voltage guarantee at
every condition. Actual board/cell qualification is still required.

Both registers are read-modify-written and read back before CE goes low. Preserve
termination enable, the safety timer and existing precharge/termination settings;
disable only the host watchdog so LED operation needs no periodic I2C kicks.
Input USB current limit is a different setting and is not changed here. Selected
current is a nominal fast-charge setpoint, not guaranteed current through the cell.

Reference: [TI BQ24295 Rev B, REG02/REG04 and electrical characteristics](https://www.ti.com/lit/ds/symlink/bq24295.pdf).
No battery test, charge accuracy, thermal behavior or electrical safety was verified.

## Recovery and rollback

Keep the firmware and Python client on the same contract. Roll back them together
using a reviewed source snapshot; do not pair a CF1 client with this firmware.
This task has no partition/NVS-format/security-fuse change. Restoring source does
not restore charger registers; first request CE disabled and follow the approved
board recovery procedure. Historical evidence and reference snapshots stay intact.

## Owner-directed LED control update (2026-10-27)

Current SET_FS accepts every integer2..10Hz, default5Hz, while STOPPED only.
The advisory INFO capability includes fs_min=2, fs_max=10, duration=HOST,
limit_s=0 and single_run=0. The host selects run duration and repeated START is
allowed after completed STOP/cleanup; explicit fault-clear and all fault/lease
protections remain. See [the revision](../LED_HOST_CONTROL.md). Older discrete
rate/2s/single-attempt descriptions are historical.
