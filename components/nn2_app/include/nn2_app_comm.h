#ifndef NN2_APP_COMM_H
#define NN2_APP_COMM_H
#include "nn2_core_protocol.h"
#include "nn2_port_safety.h"
#include "nn2_port_optical.h"
#define NN2_APP_REPLY_CACHE_SIZE 4u
#define NN2_APP_HOST_SILENCE_US UINT64_C(1500000)
#define NN2_APP_PEER_GRACE_US UINT64_C(3000000)
#define NN2_APP_HEARTBEAT_PERIOD_US UINT64_C(500000)
/* State is memcpy'd, never cast to this byte storage. */
typedef struct {
  unsigned char opaque[4096];
} nn2_app_comm_storage_t;
typedef struct {
  uint8_t state;
  uint16_t fault;
  bool owned, grace_active;
  uint32_t requests, duplicates, lease_expirations, malformed, reserved_flags;
  uint32_t session_id, sequence;
  uint64_t boot_id;
} nn2_app_comm_status_t;
/* Single owner task, never ISR/reentry. Seed is supplied by the platform's RNG;
 * no session ID repeats within one boot; exhaustion refuses further starts.
 * Default binding has disabled emitters. The opt-in LED-only port drives real
 * emitters but still has no FRAME producer or optical measurement. SIMULATED
 * is not an assurance that emitter power is unavailable. */
nn2_status_t nn2_app_comm_init(nn2_app_comm_storage_t *, uint64_t boot_id,
                               const nn2_port_safety_t *,
                               uint32_t session_seed);
/* Optional LED-only bring-up adapter. Bind once while idle, before commands.
 * START/STOP then acknowledge STARTING/STOPPING; tick observes completion.
 * Frames/ADC remain unsupported and replies remain explicitly SIMULATED. */
nn2_status_t nn2_app_comm_bind_optical(nn2_app_comm_storage_t *,
                                     const nn2_port_optical_t *);
nn2_status_t nn2_app_comm_tick(nn2_app_comm_storage_t *, uint64_t now_us,
                               bool link_up);
size_t nn2_app_comm_process(nn2_app_comm_storage_t *, uint64_t peer,
                            uint64_t now_us, const uint8_t *input,
                            size_t length, uint8_t *output, size_t capacity);
nn2_app_comm_status_t nn2_app_comm_get_status(const nn2_app_comm_storage_t *);
/* USB safety intake: same state/safety owner as UDP, no second state machine.
 */
size_t nn2_app_comm_frame(nn2_app_comm_storage_t *, uint64_t *peer, uint8_t *, size_t);
nn2_status_t nn2_app_comm_safe_off(nn2_app_comm_storage_t *);
/* Stamp only uncached time queries immediately before send; state-changing
 * cached replies remain byte-identical. Not a hardware transmit timestamp. */
void nn2_app_comm_stamp_tx(uint8_t *reply, size_t length, uint64_t now_us);
#endif
