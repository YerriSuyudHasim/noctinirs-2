# UDP Packet Compatibility

## Transport

- Control commands: UDP destination port 3333.
- Binary stream: UDP destination port 3334.
- The stream task sends from a normal UDP socket; the source port is not part of
  the compatibility contract.
- Each UDP datagram contains exactly one stream-v1 record.

## Stream-v1 envelope

The envelope remains 12 bytes and uses the v16 field order:

| Offset | Size | Field | Encoding |
|---:|---:|---|---|
| 0 | 1 | magic | `0xA5` |
| 1 | 1 | format version | `1` |
| 2 | 1 | record type | v16 record identifier |
| 3 | 1 | flags | bit 0 = CRC16 present |
| 4 | 2 | sequence | little-endian `uint16_t` |
| 6 | 4 | relative timestamp us | little-endian `uint32_t` |
| 10 | 2 | payload length | little-endian `uint16_t` |
| 12 | N | payload | record-specific packed layout |
| 12+N | 2 | CRC16 | CRC16/CCITT-FALSE, transmitted MSB then LSB |

Header and multibyte payload fields retain the ESP32/v16 little-endian layout.
The CRC bytes are appended big-endian as in the existing host contract.

## Records used by final fNIRS stream

| Type | ID | Payload | Size |
|---|---:|---|---:|
| SESSION | 8 | session v1: version, session ID, `t0_us` | 16 B |
| TIME64 | 3 | absolute timestamp encoded network-order bytes | 8 B |
| FNIRS_CFG | 6 | fNIRS configuration v2 | 16 B |
| FNIRS_FRAME | 7 | phase frame v2 | 356 B |

Two FNIRS_FRAME records are sent for each completed optical frame:

1. LED-ON intensity record.
2. `I_LED_OFF` intensity record with `LED_OFF` flag.

Both carry the paired flag. The payload includes 20×4 averages, `n_used`, valid,
under-K, and CRC masks. `frame_id` remains 16-bit for compatibility and wraps
naturally.

## Session sequencing

- Record sequence resets to zero for each stream session metadata sequence.
- Relative timestamp is `timestamp_us - t0_us`, truncated to 32 bits as in v16.
- SESSION and periodic TIME64 records provide absolute-time re-anchoring.
- Metadata is resent after stream reset or session change.

## Extension policy

The 12-byte envelope and existing record IDs are frozen. Future fields must use:

- a new payload version;
- unused/reserved bits with documented semantics; or
- a new record type.

The host parser in `tools/nocti_protocol.py` and the C compatibility test in
`tools/test_stream_compat.c` validate the current layout and CRC behavior.
