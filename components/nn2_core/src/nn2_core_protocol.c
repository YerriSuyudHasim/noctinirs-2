#include "nn2_core_protocol.h"
#include <string.h>
_Static_assert(NN2_CORE_HEADER_SIZE + NN2_CORE_PAYLOAD_MAX + 4u ==
                   NN2_CORE_PACKET_MAX,
               "MTU");
uint16_t nn2_core_get_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
uint32_t nn2_core_get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
uint64_t nn2_core_get_u64(const uint8_t *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < 8; i++)
    v |= (uint64_t)p[i] << (i * 8u);
  return v;
}
void nn2_core_put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
void nn2_core_put_u32(uint8_t *p, uint32_t v) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = (uint8_t)(v >> (8u * i));
}
void nn2_core_put_u64(uint8_t *p, uint64_t v) {
  for (unsigned i = 0; i < 8; i++)
    p[i] = (uint8_t)(v >> (8u * i));
}
uint32_t nn2_core_crc32(const uint8_t *p, size_t n) {
  uint32_t c = UINT32_MAX;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (unsigned b = 0; b < 8; b++)
      c = (c >> 1) ^ ((c & 1u) ? UINT32_C(0xedb88320) : 0u);
  }
  return ~c;
}
bool nn2_core_packet_decode(const uint8_t *p, size_t n,
                            nn2_core_packet_t *out) {
  if (!p || !out || n < 32 || n > NN2_CORE_PACKET_MAX || memcmp(p, "NNS2", 4) ||
      p[4] != 1 || p[5] > NN2_MSG_EVENT)
    return false;
  uint16_t len = nn2_core_get_u16(p + 8);
  if (len > NN2_CORE_PAYLOAD_MAX || n != 32u + len ||
      nn2_core_get_u32(p + n - 4) != nn2_core_crc32(p, n - 4))
    return false;
  nn2_core_packet_t v = {.type = p[5],
                         .protocol_version = nn2_core_get_u16(p + 6),
                         .length = len,
                         .flags = nn2_core_get_u16(p + 10),
                         .session_id = nn2_core_get_u32(p + 12),
                         .sequence = nn2_core_get_u32(p + 16),
                         .timestamp_us = nn2_core_get_u64(p + 20)};
  memcpy(v.payload, p + 28, len);
  *out = v;
  return true;
}
size_t nn2_core_packet_encode(const nn2_core_packet_t *p, uint8_t *out,
                              size_t cap) {
  if (!p || !out || p->length > NN2_CORE_PAYLOAD_MAX ||
      p->type > NN2_MSG_EVENT || cap < 32u + p->length)
    return 0;
  memcpy(out, "NNS2", 4);
  out[4] = 1;
  out[5] = p->type;
  nn2_core_put_u16(out + 6, p->protocol_version);
  nn2_core_put_u16(out + 8, p->length);
  nn2_core_put_u16(out + 10, p->flags);
  nn2_core_put_u32(out + 12, p->session_id);
  nn2_core_put_u32(out + 16, p->sequence);
  nn2_core_put_u64(out + 20, p->timestamp_us);
  memcpy(out + 28, p->payload, p->length);
  nn2_core_put_u32(out + 28 + p->length, nn2_core_crc32(out, 28u + p->length));
  return 32u + p->length;
}
static const nn2_core_command_t commands[] = {
    {NN2_CMD_HEARTBEAT, 8, false},     {NN2_CMD_INFO, 0, false},
    {NN2_CMD_STATUS, 8, false},        {NN2_CMD_GET_CONFIG, 0, false},
    {NN2_CMD_GET_TIME, 8, false},      {NN2_CMD_START, 0, true},
    {NN2_CMD_STOP, 0, true},           {NN2_CMD_SAFE_OFF, 0, false},
    {NN2_CMD_CLEAR_FAULT, 0, true},    {NN2_CMD_SET_FS, 1, true},
    {NN2_CMD_SET_LED_CURRENT, 3, true}, {NN2_CMD_GET_HEALTH, 0, false},
    {NN2_CMD_GET_TIMING, 0, false}};
const nn2_core_command_t *nn2_core_command(uint16_t opcode) {
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
    if (commands[i].opcode == opcode)
      return &commands[i];
  return NULL;
}
