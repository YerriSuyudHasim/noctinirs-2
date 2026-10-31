/* ADR-0004 envelope and command registry; pure C11, explicit LE encoding. */
#ifndef NN2_CORE_PROTOCOL_H
#define NN2_CORE_PROTOCOL_H
#include "nn2_core_types.h"
#define NN2_CORE_PACKET_MAX 1472u
#define NN2_CORE_PAYLOAD_MAX 1440u
#define NN2_CORE_HEADER_SIZE 28u
#define NN2_CORE_PROTOCOL_VERSION 1u
#define NN2_FLAG_SIMULATED 1u
#define NN2_FLAG_TS_ESTIMATED 2u
#define NN2_REQUEST_HEAD_SIZE 6u
#define NN2_REPLY_HEAD_SIZE 8u

enum {
  NN2_MSG_REQUEST,
  NN2_MSG_REPLY,
  NN2_MSG_FRAME,
  NN2_MSG_SESSION_REPORT,
  NN2_MSG_EVENT
};
enum {
  NN2_CMD_HEARTBEAT = 0x0001,
  NN2_CMD_INFO = 0x0002,
  NN2_CMD_STATUS = 0x0003,
  NN2_CMD_GET_CONFIG = 0x0004,
  NN2_CMD_GET_TIME = 0x0005,
  NN2_CMD_START = 0x0010,
  NN2_CMD_STOP = 0x0011,
  NN2_CMD_SAFE_OFF = 0x0020,
  NN2_CMD_CLEAR_FAULT = 0x0021,
  NN2_CMD_SET_FS = 0x0100,
  NN2_CMD_SET_LED_CURRENT = 0x0101,
  NN2_CMD_SET_MODE = 0x0102, /* 0 ACQ normal, 1 ENGINEERING; stopped only */
  /* Read-only V4 diagnostics. Existing reply layouts are unchanged. */
  NN2_CMD_GET_HEALTH = 0x0030,
  NN2_CMD_GET_TIMING = 0x0031
};
enum {
  NN2_REPLY_OK,
  NN2_REPLY_STATE,
  NN2_REPLY_ARGUMENT,
  NN2_REPLY_OPCODE,
  NN2_REPLY_SAFETY,
  NN2_REPLY_BUSY,
  NN2_REPLY_BAD_FRAME,
  NN2_REPLY_PRECONDITION,
  NN2_REPLY_RESERVED_FLAGS
};
enum {
  NN2_COMM_BOOT,
  NN2_COMM_IDLE,
  NN2_COMM_STARTING,
  NN2_COMM_STREAMING,
  NN2_COMM_STOPPING,
  NN2_COMM_RECOVERING,
  NN2_COMM_FAULT,
  NN2_COMM_SAFE_OFF
};
typedef struct {
  uint8_t type;
  uint16_t protocol_version, flags, length;
  uint32_t session_id, sequence;
  uint64_t timestamp_us;
  uint8_t payload[NN2_CORE_PAYLOAD_MAX];
} nn2_core_packet_t;
typedef struct {
  uint16_t opcode, argument_size;
  bool cached;
} nn2_core_command_t;
const nn2_core_command_t *nn2_core_command(uint16_t opcode);
uint16_t nn2_core_get_u16(const uint8_t *p);
uint32_t nn2_core_get_u32(const uint8_t *p);
uint64_t nn2_core_get_u64(const uint8_t *p);
void nn2_core_put_u16(uint8_t *p, uint16_t v);
void nn2_core_put_u32(uint8_t *p, uint32_t v);
void nn2_core_put_u64(uint8_t *p, uint64_t v);
uint32_t nn2_core_crc32(const uint8_t *data, size_t length);
/* Decode framing first; application checks protocol version/flags after CRC so
 * a parsable request can receive BAD_FRAME/RESERVED_FLAGS. Output is atomic. */
bool nn2_core_packet_decode(const uint8_t *data, size_t length,
                            nn2_core_packet_t *out);
size_t nn2_core_packet_encode(const nn2_core_packet_t *packet, uint8_t *out,
                              size_t capacity);
#endif
