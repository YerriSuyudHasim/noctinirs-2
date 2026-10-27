> SUPERSEDED WIRE CONTRACT (2026-10-27): active source now uses ADR-0004/1.
> See [the current local implementation binding](adr/0004-local-implementation.md).
> CF1 packets, CLAIM/RELEASE and the examples below are historical and are not
> accepted by the current firmware/client. SoftAP-only remains the network mode.

# NN2-CF1 local wire contract

> CURRENT OWNER DECISION: v0.1.0 is SoftAP-only. See [SOFTAP_CURRENT.md](SOFTAP_CURRENT.md)
> for active settings, APIs, setup and acceptance changes. Earlier STA/AP+STA,
> provisioning and v0.1.0.1 sections below are superseded historical records.
> This SoftAP source revision has not been built or board-tested.

> Current implementation: USB provisioning + STA. The approved SoftAP/STA revision
> is specified in NETWORK_ONBOARDING.md and is not implemented yet. Existing
> NN2-CF1 operational packets remain unchanged.

Local Communication Foundation protocol, version 1, 2026-10-22. This contract and
the new CLI are paired. V3/V4 ASCII control and their stream frames are not accepted.
ADR-0004 is absent from this checkout; this is an isolated local contract, not an
assertion of compatibility with that unseen Proposed document.

## Packet envelope

UDP/IPv4 port 3333 by default (Kconfig configurable), one packet/datagram. Integers
are unsigned and little endian. No packed structs or implicit native layout.

| Offset | Bytes | Field |
|---|---|---|
| 0 | 2 | ASCII `N2` |
| 2 | 1 | Version, exactly 1 |
| 3 | 1 | Flags: 0 request, 1 reply; all other values rejected |
| 4 | 4 | Nonzero request ID |
| 8 | 2 | Opcode |
| 10 | 2 | Payload length, 0..128 |
| 12 | N | Payload |
| 12+N | 2 | CRC-16/CCITT-FALSE, little-endian CRC value |

CRC covers header and payload: polynomial 0x1021, init 0xFFFF, no reflection,
xorout 0. Check `123456789` = 0x29B1. Maximum datagram: 142 bytes.
Golden vectors: `test/communication/golden_vectors.json`.

Bad magic/version/flags/CRC/length, zero ID, incoming replies, truncation and
oversize get no response and no command side effect. Valid envelopes with unknown
opcodes or invalid arguments receive explicit errors. Endpoint is IPv4 AND port.

## Reply payload

All replies begin with the following 20 bytes; errors have no additional data.
All v0.1.0 replies mark simulation capability explicitly, including actual
hardware-independent queries. This flag does not pretend off protection is a mock.

| Offset | Bytes | Meaning |
|---|---|---|
| 0 | 1 | Result: 0 OK, 1 OPCODE, 2 ARGUMENT, 3 STATE, 4 PEER, 5 ID_CONFLICT, 6 SAFETY, 7 STALE_ID |
| 1 | 1 | State: 0 IDLE, 1 SIMULATED_RUNNING, 2 FAULT |
| 2 | 1 | SIMULATED capability = 1 |
| 3 | 1 | Reserved = 0 |
| 4 | 8 | Persistent boot counter, scoped to this board's NVS history |
| 12 | 8 | Monotonic device microseconds at first execution |

Clock source: esp_timer_get_time(). Not UTC, ADC conversion time, DRDY time or
optical timing evidence. It restarts at boot. The boot counter is committed once
per startup before networking; no counter allocation means no network start.
Erasing/restoring NVS invalidates prior identity history; identify the physical
unit separately. Cached replies contain their original timestamp. Host round-trip
time bounds only the exchange, and retries cannot be used as an unbiased offset
measurement. No time-sync accuracy is promised.

## Command registry

`Owner` means the source endpoint currently owns the lease. Others are public.
Empty means exactly zero request payload bytes.

| Opcode | Command | Request | Access | Success data after reply prefix |
|---|---|---|---|---|
| 1 | INFO | Empty | Public | ASCII firmware/protocol/capability string |
| 2 | STATUS | Empty | Public | u32 requests, u32 duplicates, u32 lease losses, u8 fault, u8 caller-is-owner |
| 3 | CLAIM | Empty | Public | None; fails PEER if another endpoint owns |
| 4 | HEARTBEAT | Empty | Owner | None; renews lease only on new execution |
| 5 | GET_CONFIG | Empty | Public | u16 simulated rate Hz, u8 slots, u8 zero |
| 6 | SET_CONFIG | Same 4-byte layout | Owner | None; IDLE only, rate 1..10, slots 1..20 |
| 7 | START | Empty | Owner | None; IDLE -> SIMULATED_RUNNING, reasserts real off |
| 8 | STOP | Empty | Owner | None; requests off, running -> IDLE, preserves FAULT |
| 9 | SAFE_OFF | Empty | Public | None; requests off, preserves fault and lease |
| 10 | FAULT | u8 synthetic code 1..253 | Owner | None; requests off, enters FAULT |
| 11 | CLEAR_FAULT | Empty | Owner | None; off must succeed; -> IDLE |
| 12 | GET_TIME | Empty | Public | None; use boot/time in prefix |
| 13 | RELEASE | Empty | Owner | None; requests off, clears ownership/cache |
| 14 | GET_FAULT | Empty | Public | u8 code, u8 synthetic-fault flag |

Fault 254: backward clock, reboot required. Fault 255: off callback failure.
No FAULT command may mask an existing fault. Configuration above is RAM-only
simulation configuration, not Wi-Fi credentials, electrical limits or slot timing.
Defaults are rate=10, slots=20. It does not start a producer or stream.
Counters are modulo uint32; STATUS captures counts before counting its own request.

## Transactions, ownership and recovery

Eight FIFO entries hold full request/reply bytes, keyed by endpoint+ID. Identical
duplicates return exact original bytes without re-executing or refreshing a lease.
A reused ID with different bytes returns ID_CONFLICT without replacing the cache.
Errors are cached too; retry with a new ID after changing circumstances.

Owner commands/CLAIM IDs must advance modulo uint32 with delta in 1..2^31-1, skipping
zero. Their last accepted ordering position survives eviction, so an old mutation
cannot execute again merely because public queries evicted its cached response.
Public queries need fresh IDs too, but are not ordering barriers. IDs are not a
cryptographic replay defence. Only one transaction should be outstanding per host.

Lease anchor is successful CLAIM or newly executed HEARTBEAT; other traffic and
duplicates do not refresh it. At `now-anchor >= 3,000,000 us`, first request off,
stop simulation, clear owner and cache. A known Wi-Fi/socket loss revokes immediately
(stricter than the heartbeat grace). The service remains responsive, without a
3-second sleep. After loss/expiry, a new CLAIM is required. FAULT persists.
There are no asynchronous grace timers; one poll owner evaluates absolute time,
so no stale timer callback can stop a newly claimed session.

Poll uses the Wi-Fi link epoch as well as current level to observe quick loss/
recovery between iterations. Socket open errors retry after >=1 second. TX pressure
drops the response and increments a counter; duplicate request retries recover it.
There is no app receive queue to exhaust. SDK RX-buffer loss is accounted by host
timeouts; exact lower-layer packet-drop totals are not exposed here.

This protocol has no authentication/encryption. Use a controlled WPA2 LAN. Endpoint
ownership/CRC detect accidents and contention, not a malicious LAN peer. SAFE_OFF
is deliberately available to other endpoints and never releases reset.

## USB provisioning

`WIFI <ssid-hex> <password-hex>\n` (CRLF also accepted), max 196 bytes before LF.
SSID: 1..32 non-NUL bytes; password: 8..63 printable ASCII bytes, WPA2 PSK profile.
One versioned blob is persisted in namespace `nn2_network`, key `wifi_cf1`.
No blank/open-network password, no broad erase, no echo. `STATUS\n` reports IP,
reset, heap/stack and error counters without credentials. First provisioning starts
STA; replacements while started require reboot. Boot identity key is `boot_id`.
Partial lines expire after 10 seconds and drain through LF. Overflow also drains
through LF. Failure acknowledgement does not prove the old NVS value survived.
