/* Production ADR-0004 codec/control tests; no ESP-IDF or physical hardware. */
#include "nn2_app_comm.h"
#include "nn2_app_provision.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failures, off_calls;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      ++failures;                                                              \
      printf("FAIL %d: %s\n", __LINE__, #x);                                   \
    }                                                                          \
  } while (0)
static nn2_app_comm_storage_t state;
static uint64_t now = 1000;
static bool off_fails;
static uint8_t optical_state;
static nn2_port_optical_config_t optical_config;
static unsigned optical_starts;
static uint8_t encoded[NN2_CORE_PACKET_MAX], response[NN2_CORE_PACKET_MAX];
static size_t encoded_size, response_size;
static nn2_status_t fake_off(void *ctx) {
  (void)ctx;
  ++off_calls;
  if (optical_state == NN2_OPTICAL_STARTING || optical_state == NN2_OPTICAL_RUNNING)
    optical_state = NN2_OPTICAL_STOPPING;
  return off_fails ? NN2_ERR_IO : NN2_OK;
}
static void reset(void) {
  now = 1000;
  off_calls = 0;
  off_fails = false;
  optical_state = NN2_OPTICAL_IDLE;
  nn2_port_safety_t safety = {.hold_off = fake_off};
  CHECK(nn2_app_comm_init(&state, 42, &safety, 12345) == NN2_OK);
}
static nn2_core_packet_t send_request(uint64_t peer, uint32_t id, uint16_t op,
                                      const uint8_t *arg, uint16_t n,
                                      uint32_t session, uint16_t flags,
                                      uint16_t protocol) {
  nn2_core_packet_t req = {.type = NN2_MSG_REQUEST,
                           .protocol_version = protocol,
                           .session_id = session,
                           .flags = flags,
                           .length = (uint16_t)(6u + n)};
  nn2_core_put_u32(req.payload, id);
  nn2_core_put_u16(req.payload + 4, op);
  if (n)
    memcpy(req.payload + 6, arg, n);
  encoded_size = nn2_core_packet_encode(&req, encoded, sizeof(encoded));
  response_size = nn2_app_comm_process(&state, peer, now, encoded, encoded_size,
                                       response, sizeof(response));
  nn2_core_packet_t reply = {0};
  CHECK(response_size != 0);
  CHECK(nn2_core_packet_decode(response, response_size, &reply));
  CHECK(reply.type == NN2_MSG_REPLY && reply.flags == NN2_FLAG_SIMULATED);
  CHECK(nn2_core_get_u32(reply.payload) == id &&
        nn2_core_get_u16(reply.payload + 4) == op);
  CHECK(nn2_core_get_u64(reply.payload + 12) == 42);
  return reply;
}
static nn2_core_packet_t command(uint64_t peer, uint32_t id, uint16_t op,
                                 const uint8_t *arg, uint16_t n) {
  return send_request(peer, id, op, arg, n, 0, 0, 1);
}
static uint16_t error(nn2_core_packet_t p) {
  return nn2_core_get_u16(p.payload + 6);
}
static void transactions(void) {
  reset();
  nn2_core_packet_t defaults = command(1, 90, NN2_CMD_GET_CONFIG, NULL, 0);
  CHECK(defaults.payload[28] == 5);
  for (unsigned i = 0; i < 20; ++i)
    CHECK(nn2_core_get_u16(defaults.payload + 30 + i * 2) == 30);
  CHECK(error(command(1, 1, NN2_CMD_INFO, NULL, 0)) == 0);
  uint8_t fs = 3;
  CHECK(error(command(1, 2, NN2_CMD_SET_FS, &fs, 1)) == NN2_REPLY_ARGUMENT);
  fs = 5;
  CHECK(error(command(1, 3, NN2_CMD_SET_FS, &fs, 1)) == 0);
  uint8_t current[] = {4, 30, 0};
  CHECK(error(command(1, 4, NN2_CMD_SET_LED_CURRENT, current, 3)) == 0);
  nn2_core_packet_t config = command(1, 100, NN2_CMD_GET_CONFIG, NULL, 0);
  CHECK(config.payload[28] == 5 &&
        nn2_core_get_u16(config.payload + 30 + 4 * 2) == 30);
  nn2_core_packet_t start = command(1, 5, NN2_CMD_START, NULL, 0);
  CHECK(start.payload[8] == NN2_COMM_STREAMING && start.session_id != 0);
  uint32_t session = start.session_id;
  uint8_t first[256];
  size_t first_size = response_size;
  memcpy(first, response, first_size);
  unsigned previous = off_calls;
  uint8_t echo[8] = {0};
  CHECK(error(command(2, 1, NN2_CMD_STATUS, echo, 8)) == NN2_REPLY_STATE);
  CHECK(error(command(2, 2, NN2_CMD_GET_TIME, echo, 8)) == 0);
  for (uint32_t id = 20; id < 40; id++)
    command(2, id, NN2_CMD_INFO, NULL, 0);
  now += 100;
  CHECK(error(command(1, 5, NN2_CMD_START, NULL, 0)) == 0);
  CHECK(response_size == first_size && !memcmp(first, response, first_size) &&
        off_calls == previous);
  CHECK(error(command(1, 5, NN2_CMD_STOP, NULL, 0)) == NN2_REPLY_ARGUMENT);
  CHECK(nn2_app_comm_get_status(&state).session_id == session);
  CHECK(error(send_request(2, 50, NN2_CMD_SAFE_OFF, NULL, 0, 999, 0, 1)) == 0);
  CHECK(nn2_app_comm_get_status(&state).state == NN2_COMM_SAFE_OFF &&
        !nn2_app_comm_get_status(&state).owned);
  previous = off_calls;
  command(2, 50, NN2_CMD_SAFE_OFF, NULL, 0);
  CHECK(off_calls == previous + 1);
  CHECK(error(command(2, 51, NN2_CMD_CLEAR_FAULT, NULL, 0)) == 0);
  CHECK(error(command(1, 5, NN2_CMD_START, NULL, 0)) ==
        NN2_REPLY_ARGUMENT); /* cannot restart evicted request */
  start = command(1, 6, NN2_CMD_START, NULL, 0);
  CHECK(start.session_id && start.session_id != session);
  CHECK(error(send_request(1, 7, NN2_CMD_STOP, NULL, 0, session, 0, 1)) ==
        NN2_REPLY_STATE);
  CHECK(error(send_request(1, 7, NN2_CMD_STOP, NULL, 0, start.session_id, 0,
                           1)) == 0);
  CHECK(nn2_app_comm_get_status(&state).state == NN2_COMM_IDLE);
  /* Same ID and opcode with different arguments cannot silently change config.
   */
  fs = 4;
  CHECK(error(command(1, 8, NN2_CMD_SET_FS, &fs, 1)) == 0);
  fs = 8;
  CHECK(error(command(1, 8, NN2_CMD_SET_FS, &fs, 1)) == NN2_REPLY_ARGUMENT);
  config = command(1, 9, NN2_CMD_GET_CONFIG, NULL, 0);
  CHECK(config.payload[28] == 4);
  CHECK(nn2_app_comm_safe_off(&state) == NN2_OK);
  CHECK(nn2_app_comm_get_status(&state).state == NN2_COMM_SAFE_OFF);
}
static void time_and_grace(void) {
  reset();
  command(1, 1, NN2_CMD_START, NULL, 0);
  now += 1000000;
  CHECK(error(command(1, 2, NN2_CMD_START, NULL, 0)) == NN2_REPLY_STATE);
  now += 400000;
  CHECK(error(command(1, 2, NN2_CMD_START, NULL, 0)) == NN2_REPLY_STATE);
  now += 100000;
  CHECK(nn2_app_comm_tick(&state, now, true) == NN2_OK);
  CHECK(nn2_app_comm_get_status(&state).grace_active);
  reset();
  command(1, 1, NN2_CMD_START, NULL, 0);
  now += NN2_APP_HOST_SILENCE_US;
  CHECK(nn2_app_comm_tick(&state, now, true) == NN2_OK &&
        nn2_app_comm_get_status(&state).grace_active);
  now += NN2_APP_PEER_GRACE_US - 1;
  CHECK(nn2_app_comm_tick(&state, now, true) == NN2_OK &&
        nn2_app_comm_get_status(&state).owned);
  uint8_t echo[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  command(1, 2, NN2_CMD_HEARTBEAT, echo, 8);
  CHECK(!nn2_app_comm_get_status(&state).grace_active);
  nn2_app_comm_stamp_tx(response, response_size, now + 7);
  nn2_core_packet_t stamped = {0};
  CHECK(nn2_core_packet_decode(response, response_size, &stamped));
  CHECK(nn2_core_get_u64(stamped.payload + 44) == now + 7 &&
        stamped.timestamp_us == now + 7);
  CHECK(!memcmp(stamped.payload + 28, echo, 8));
  ++now;
  CHECK(nn2_app_comm_tick(&state, now, false) == NN2_OK);
  ++now;
  CHECK(nn2_app_comm_tick(&state, now, true) == NN2_OK &&
        nn2_app_comm_get_status(&state).grace_active);
  now += NN2_APP_PEER_GRACE_US;
  CHECK(nn2_app_comm_tick(&state, now, true) == NN2_OK &&
        !nn2_app_comm_get_status(&state).owned);
  CHECK(nn2_app_comm_get_status(&state).state == NN2_COMM_IDLE);
  command(1, 3, NN2_CMD_START, NULL, 0);
  off_fails = true;
  CHECK(error(command(2, 4, NN2_CMD_SAFE_OFF, NULL, 0)) == NN2_REPLY_SAFETY);
  CHECK(nn2_app_comm_get_status(&state).state == NN2_COMM_FAULT);
  CHECK(error(command(2, 5, NN2_CMD_CLEAR_FAULT, NULL, 0)) == NN2_REPLY_SAFETY);
  off_fails = false;
  CHECK(error(command(2, 6, NN2_CMD_CLEAR_FAULT, NULL, 0)) == 0);
  CHECK(nn2_app_comm_tick(&state, now - 1, true) == NN2_ERR_INVALID_STATE);
  CHECK(error(command(2, 7, NN2_CMD_CLEAR_FAULT, NULL, 0)) ==
        NN2_REPLY_PRECONDITION);
  reset();
  command(1, UINT32_MAX - 1, NN2_CMD_START, NULL, 0);
  CHECK(error(command(1, UINT32_MAX, NN2_CMD_STOP, NULL, 0)) == 0);
  CHECK(error(command(1, 0, NN2_CMD_START, NULL, 0)) == 0);
}
static void validation(void) {
  static const uint8_t golden[] = {
      0x4e, 0x4e, 0x53, 0x32, 0x01, 0x00, 0x01, 0x00, 0x06, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56,
      0x34, 0x12, 0x02, 0x00, 0x93, 0xa5, 0x7b, 0x42};
  nn2_core_packet_t gv = {0};
  CHECK(nn2_core_packet_decode(golden, sizeof(golden), &gv));
  CHECK(nn2_core_get_u32(gv.payload) == UINT32_C(0x12345678) &&
        gv.sequence == 1);
  CHECK(nn2_core_packet_encode(&gv, encoded, sizeof(encoded)) ==
        sizeof(golden));
  CHECK(!memcmp(encoded, golden, sizeof(golden)));
  reset();
  CHECK(nn2_core_crc32((const uint8_t *)"123456789", 9) ==
        UINT32_C(0xcbf43926));
  CHECK(error(command(1, 1, 0xffff, NULL, 0)) == NN2_REPLY_OPCODE);
  CHECK(error(send_request(1, 2, NN2_CMD_START, NULL, 0, 0, 2, 1)) ==
        NN2_REPLY_RESERVED_FLAGS);
  CHECK(error(send_request(1, 3, NN2_CMD_START, NULL, 0, 0, 0, 2)) ==
        NN2_REPLY_BAD_FRAME);
  CHECK(!nn2_app_comm_get_status(&state).owned);
  CHECK(error(command(1, 2, NN2_CMD_START, NULL, 0)) ==
        0); /* malformed request never poisons cache */
  command(1, 4, NN2_CMD_STOP, NULL, 0);
  for (size_t n = 0; n < encoded_size; n++)
    CHECK(nn2_app_comm_process(&state, 1, now, encoded, n, response,
                               sizeof(response)) == 0);
  encoded[encoded_size - 1] ^= 1;
  CHECK(nn2_app_comm_process(&state, 1, now, encoded, encoded_size, response,
                             sizeof(response)) == 0);
  uint8_t junk[NN2_CORE_PACKET_MAX + 1] = {0};
  CHECK(nn2_app_comm_process(&state, 1, now, junk, sizeof(junk), response,
                             sizeof(response)) == 0);
  CHECK(nn2_app_comm_process(&state, 1, now, (const uint8_t *)"START\n", 6,
                             response, sizeof(response)) == 0);
  nn2_core_packet_t p = {.type = NN2_MSG_FRAME,
                         .protocol_version = 1,
                         .length = 1440},
                    out = {0};
  size_t n = nn2_core_packet_encode(&p, encoded, sizeof(encoded));
  CHECK(n == 1472);
  CHECK(nn2_core_packet_decode(encoded, n, &out) && out.length == 1440);
  CHECK(nn2_app_comm_process(&state, 1, now, encoded, n, response,
                             sizeof(response)) == 0);
  CHECK(nn2_core_packet_encode(&p, encoded, n - 1) == 0);
  uint8_t current[] = {20, 30, 0};
  CHECK(error(command(1, 5, NN2_CMD_SET_LED_CURRENT, current, 3)) ==
        NN2_REPLY_ARGUMENT);
  current[0] = 0;
  current[1] = 51;
  CHECK(error(command(1, 6, NN2_CMD_SET_LED_CURRENT, current, 3)) ==
        NN2_REPLY_ARGUMENT);
}
/* Retain the pre-existing standalone provisioning regression, even though the
 * current SoftAP USB adapter rejects WIFI lines. Public synthetic fixture only.
 */
static nn2_network_config_t stored;
static bool store_fails;
static unsigned store_calls;
static nn2_status_t store(void *ctx, const nn2_network_config_t *value) {
  (void)ctx;
  ++store_calls;
  if (store_fails)
    return NN2_ERR_IO;
  stored = *value;
  return NN2_OK;
}
static void provisioning(void) {
  nn2_port_network_config_t port = {.save = store};
  nn2_network_config_t output = {0};
  const char line[] = "WIFI 74657374 6162636465666768";
  store_fails = false;
  CHECK(nn2_app_provision(line, sizeof(line) - 1, &port, &output) == NN2_OK);
  CHECK(output.ssid_length == 4 && output.password_length == 8);
  CHECK(!memcmp(&stored, &output, sizeof(output)));
  nn2_network_config_t previous = output;
  store_fails = true;
  CHECK(nn2_app_provision(line, sizeof(line) - 1, &port, &output) ==
        NN2_ERR_IO);
  CHECK(!memcmp(&previous, &output, sizeof(output)));
  unsigned calls = store_calls;
  CHECK(nn2_app_provision("WIFI 00 ff", 10, &port, &output) ==
        NN2_ERR_INVALID_ARG);
  CHECK(nn2_app_provision(line, sizeof(line) - 2, &port, &output) ==
        NN2_ERR_INVALID_ARG);
  CHECK(store_calls == calls);
}
static nn2_status_t optical_start(void *ctx, const nn2_port_optical_config_t *config) {
  (void)ctx;
  optical_config = *config;
  optical_state = NN2_OPTICAL_STARTING;
  ++optical_starts;
  return NN2_OK;
}
static uint8_t optical_get_state(void *ctx) { (void)ctx; return optical_state; }
static nn2_status_t optical_clear(void *ctx) {
  (void)ctx;
  if (optical_state == NN2_OPTICAL_STOPPING) return NN2_ERR_INVALID_STATE;
  optical_state = NN2_OPTICAL_IDLE;
  return NN2_OK;
}
static void optical_binding(void) {
  reset(); optical_starts = 0;
  nn2_port_optical_t port = {.start = optical_start, .state = optical_get_state,
      .clear_fault = optical_clear, .current_limit_ma = 30};
  port.policy_info = "led=REAL_BRINGUP;short_policy=BENCH_WARN;limit_s=2;single_run=1";
  CHECK(nn2_app_comm_bind_optical(&state, &port) == NN2_OK);
  nn2_core_packet_t info = command(1, 0, NN2_CMD_INFO, NULL, 0);
  CHECK(info.length == 28 + strlen(port.policy_info));
  CHECK(!memcmp(info.payload + 28, port.policy_info, strlen(port.policy_info)));
  nn2_core_packet_t started = command(1, 1, NN2_CMD_START, NULL, 0);
  CHECK(error(started) == 0 && started.payload[8] == NN2_COMM_STARTING);
  CHECK(optical_config.fs == 5 && optical_config.current_ma[19] == 30 && optical_starts == 1);
  CHECK(error(command(1, 1, NN2_CMD_START, NULL, 0)) == 0 && optical_starts == 1);
  uint8_t fs = 10;
  CHECK(error(command(1, 2, NN2_CMD_SET_FS, &fs, 1)) == NN2_REPLY_STATE);
  optical_state = NN2_OPTICAL_RUNNING;
  CHECK(nn2_app_comm_tick(&state, now, true) == NN2_OK);
  CHECK(nn2_app_comm_get_status(&state).state == NN2_COMM_STREAMING);
  nn2_core_packet_t stopped = command(1, 3, NN2_CMD_STOP, NULL, 0);
  CHECK(stopped.payload[8] == NN2_COMM_STOPPING);
  CHECK(nn2_app_comm_get_status(&state).owned);
  optical_state = NN2_OPTICAL_IDLE;
  CHECK(nn2_app_comm_tick(&state, now, true) == NN2_OK);
  CHECK(nn2_app_comm_get_status(&state).state == NN2_COMM_IDLE &&
        !nn2_app_comm_get_status(&state).owned);
  nn2_core_packet_t retry = command(1, 3, NN2_CMD_STOP, NULL, 0);
  CHECK(error(retry) == 0 && retry.payload[8] == NN2_COMM_STOPPING);
  CHECK(error(command(1, 4, NN2_CMD_SET_FS, &fs, 1)) == 0);
  uint8_t current[3] = {0, 31, 0};
  CHECK(error(command(1, 5, NN2_CMD_SET_LED_CURRENT, current, 3)) == NN2_REPLY_ARGUMENT);
  CHECK(error(command(1, 6, NN2_CMD_START, NULL, 0)) == 0);
  CHECK(optical_config.fs == 10);
  nn2_core_packet_t safe = command(2, 1, NN2_CMD_SAFE_OFF, NULL, 0);
  CHECK(safe.payload[8] == NN2_COMM_SAFE_OFF);
  CHECK(optical_state == NN2_OPTICAL_STOPPING);
  CHECK(error(command(2, 2, NN2_CMD_CLEAR_FAULT, NULL, 0)) == NN2_REPLY_PRECONDITION);
  optical_state = NN2_OPTICAL_IDLE;
  CHECK(error(command(2, 3, NN2_CMD_CLEAR_FAULT, NULL, 0)) == 0);
  CHECK(error(command(2, 4, NN2_CMD_START, NULL, 0)) == 0);
  optical_state = NN2_OPTICAL_FAULT;
  CHECK(nn2_app_comm_tick(&state, now, true) == NN2_OK);
  CHECK(nn2_app_comm_get_status(&state).state == NN2_COMM_FAULT &&
        nn2_app_comm_get_status(&state).fault != 0 && !nn2_app_comm_get_status(&state).owned);
  CHECK(error(command(2, 5, NN2_CMD_CLEAR_FAULT, NULL, 0)) == 0);
}
int main(void) {
  transactions();
  time_and_grace();
  validation();
  provisioning();
  optical_binding();
  printf("%u checks, %u failures\n", checks, failures);
  return failures ? 1 : 0;
}
