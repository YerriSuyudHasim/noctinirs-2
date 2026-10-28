#include "nn2_app_comm.h"
#include "nn2_core_acquisition.h"
#include <string.h>
#define MAGIC UINT32_C(0x41445234)
#define REPLY_MAX 256u
#define FAULT_OFF 0x0102u
#define FAULT_CLOCK 0x010Au
#define FAULT_OPTICAL 0x010Bu
typedef struct {
  uint64_t peer;
  uint32_t id, session;
  uint16_t opcode, size, reply_size;
  uint8_t args[3], reply[REPLY_MAX];
} cache_t;
typedef struct {
  uint64_t peer;
  uint32_t id;
  bool valid;
} watermark_t;
typedef struct {
  uint32_t magic, seed, session, sequence, requests, duplicates, expired,
      malformed, reserved;
  uint64_t boot, owner, anchor, last_now, grace_deadline, starts;
  nn2_port_safety_t safety;
  nn2_port_optical_t optical;
  uint16_t fault, current_ma[20];
  uint8_t state, fs, next, next_watermark;
  bool grace;
  cache_t cache[NN2_APP_REPLY_CACHE_SIZE];
  watermark_t watermark[4];
} comm_t;
_Static_assert(sizeof(comm_t) <= sizeof(nn2_app_comm_storage_t),
               "communication storage");
_Static_assert(NN2_APP_HOST_SILENCE_US > 2 * NN2_APP_HEARTBEAT_PERIOD_US,
               "heartbeat/silence");
static bool load(const nn2_app_comm_storage_t *s, comm_t *c) {
  if (!s)
    return false;
  memcpy(c, s->opaque, sizeof(*c));
  return c->magic == MAGIC;
}
static void save(nn2_app_comm_storage_t *s, const comm_t *c) {
  memcpy(s->opaque, c, sizeof(*c));
}
static nn2_status_t off(comm_t *c) {
  if (c->safety.hold_off(c->safety.ctx) == NN2_OK)
    return NN2_OK;
  c->fault = FAULT_OFF;
  c->state = NN2_COMM_FAULT;
  c->owner = 0;
  c->session = 0;
  c->grace = false;
  memset(c->cache, 0, sizeof(c->cache));
  return NN2_ERR_IO;
}
static void end_session(comm_t *c, uint8_t state) {
  c->owner = 0;
  c->session = 0;
  c->grace = false;
  c->state = state;
  memset(c->cache, 0, sizeof(c->cache));
  c->next = 0;
}
nn2_status_t nn2_app_comm_safe_off(nn2_app_comm_storage_t *s) {
  comm_t c;
  if (!load(s, &c))
    return NN2_ERR_INVALID_STATE;
  nn2_status_t r = off(&c);
  if (r == NN2_OK)
    end_session(&c, NN2_COMM_SAFE_OFF);
  save(s, &c);
  return r;
}
static nn2_status_t tick(comm_t *c, uint64_t now, bool link) {
  if (now < c->last_now) {
    (void)off(c);
    if (!c->fault)
      c->fault = FAULT_CLOCK;
    end_session(c, NN2_COMM_FAULT);
    return NN2_ERR_INVALID_STATE;
  }
  c->last_now = now;
  if (c->optical.state) {
    uint8_t state = c->optical.state(c->optical.ctx);
    if (state == NN2_OPTICAL_FAULT && c->state != NN2_COMM_FAULT) {
      (void)off(c);
      if (!c->fault) c->fault = FAULT_OPTICAL;
      end_session(c, NN2_COMM_FAULT);
    } else if (c->state == NN2_COMM_STARTING && state == NN2_OPTICAL_RUNNING) {
      c->state = NN2_COMM_STREAMING;
    } else if (c->state == NN2_COMM_STOPPING && state == NN2_OPTICAL_IDLE) {
      /* Keep the original STOP acceptance ACK after asynchronous drain. A
       * retry must not fail just because its session has since been released. */
      for (unsigned i = 0; i < NN2_APP_REPLY_CACHE_SIZE; ++i) {
        cache_t *entry = &c->cache[i];
        if (!entry->reply_size || entry->opcode != NN2_CMD_STOP ||
            entry->peer != c->owner ||
            nn2_core_get_u32(entry->reply + 12) != c->session ||
            nn2_core_get_u16(entry->reply + 34) != NN2_REPLY_OK)
          memset(entry, 0, sizeof(*entry));
      }
      c->owner = 0;
      c->session = 0;
      c->grace = false;
      c->state = NN2_COMM_IDLE;
      c->next = 0;
      for (unsigned i = 0; i < NN2_APP_REPLY_CACHE_SIZE; ++i)
        if (!c->cache[i].reply_size) { c->next = (uint8_t)i; break; }
    }
  }
  if (c->owner) {
    if (!c->grace && (!link || now - c->anchor >= NN2_APP_HOST_SILENCE_US)) {
      c->grace = true;
      uint64_t start = (!link) ? now : c->anchor + NN2_APP_HOST_SILENCE_US;
      c->grace_deadline = start + NN2_APP_PEER_GRACE_US;
    }
    if (c->grace && now >= c->grace_deadline) {
      nn2_status_t r = off(c);
      ++c->expired;
      end_session(c, r == NN2_OK ? NN2_COMM_IDLE : NN2_COMM_FAULT);
      return r;
    }
  }
  return NN2_OK;
}
nn2_status_t nn2_app_comm_init(nn2_app_comm_storage_t *s, uint64_t boot,
                               const nn2_port_safety_t *safety, uint32_t seed) {
  if (!s || !boot || !safety || !safety->hold_off)
    return NN2_ERR_INVALID_ARG;
  comm_t c = {.magic = MAGIC,
              .boot = boot,
              .seed = seed,
              .safety = *safety,
              .fs = NN2_OPTICAL_DEFAULT_FS,
              .state = NN2_COMM_IDLE};
  for (unsigned i = 0; i < NN2_OPTICAL_SLOTS; ++i)
    c.current_ma[i] = NN2_OPTICAL_BRINGUP_MA;
  nn2_status_t r = off(&c);
  memset(s, 0, sizeof(*s));
  save(s, &c);
  return r;
}
nn2_status_t nn2_app_comm_bind_optical(nn2_app_comm_storage_t *s,
                                      const nn2_port_optical_t *port) {
  comm_t c;
  if (!port || !port->start || !port->state || !port->clear_fault ||
      !port->current_limit_ma || port->current_limit_ma > 50 || !load(s, &c))
    return NN2_ERR_INVALID_ARG;
  if (c.owner || c.state != NN2_COMM_IDLE || c.optical.state ||
      port->state(port->ctx) != NN2_OPTICAL_IDLE) return NN2_ERR_INVALID_STATE;
  for (unsigned i = 0; i < NN2_OPTICAL_SLOTS; ++i)
    if (c.current_ma[i] > port->current_limit_ma) return NN2_ERR_INVALID_ARG;
  c.optical = *port;
  save(s, &c);
  return NN2_OK;
}
nn2_status_t nn2_app_comm_tick(nn2_app_comm_storage_t *s, uint64_t now,
                               bool link) {
  comm_t c;
  if (!load(s, &c))
    return NN2_ERR_INVALID_ARG;
  nn2_status_t r = tick(&c, now, link);
  save(s, &c);
  return r;
}
nn2_app_comm_status_t nn2_app_comm_get_status(const nn2_app_comm_storage_t *s) {
  comm_t c;
  nn2_app_comm_status_t v = {0};
  if (load(s, &c)) {
    v.state = c.state;
    v.fault = c.fault;
    v.owned = c.owner != 0;
    v.grace_active = c.grace;
    v.requests = c.requests;
    v.duplicates = c.duplicates;
    v.lease_expirations = c.expired;
    v.malformed = c.malformed;
    v.reserved_flags = c.reserved;
    v.boot_id = c.boot;
    v.session_id = c.session;
    v.sequence = c.sequence;
  }
  return v;
}
static bool fresh_mutation(comm_t *c, uint64_t peer, uint32_t id) {
  watermark_t *w = NULL;
  for (unsigned i = 0; i < 4; i++)
    if (c->watermark[i].valid && c->watermark[i].peer == peer)
      w = &c->watermark[i];
  if (w) {
    uint32_t delta = id - w->id;
    if (!delta || delta >= UINT32_C(0x80000000))
      return false;
  } else {
    w = &c->watermark[c->next_watermark];
    c->next_watermark = (uint8_t)((c->next_watermark + 1u) % 4u);
  }
  *w = (watermark_t){.peer = peer, .id = id, .valid = true};
  return true;
}
static uint16_t execute(comm_t *c, uint64_t peer, uint64_t now, uint16_t op,
                        const uint8_t *arg) {
  switch (op) {
  case NN2_CMD_START:
    if (c->state != NN2_COMM_IDLE)
      return NN2_REPLY_STATE;
    if (c->starts >= UINT32_MAX)
      return NN2_REPLY_PRECONDITION;
    if (off(c) != NN2_OK)
      return NN2_REPLY_SAFETY;
    if (c->optical.start) {
      nn2_port_optical_config_t config = {.fs = c->fs};
      memcpy(config.current_ma, c->current_ma, sizeof(config.current_ma));
      nn2_status_t result = c->optical.start(c->optical.ctx, &config);
      if (result != NN2_OK)
        return result == NN2_ERR_INVALID_ARG ? NN2_REPLY_ARGUMENT : NN2_REPLY_BUSY;
    }
    do {
      ++c->starts;
      c->session = c->seed + (uint32_t)c->starts;
    } while (!c->session && c->starts < UINT32_MAX);
    if (!c->session) {
      (void)off(c);
      return NN2_REPLY_PRECONDITION;
    }
    c->owner = peer;
    c->anchor = now;
    c->grace = false;
    c->sequence = 0;
    /* Optional adapter reports asynchronous LED readiness. Without it this is
     * immediate control simulation. Neither mode has ADC capture or FRAMEs. */
    c->state = c->optical.state ? NN2_COMM_STARTING : NN2_COMM_STREAMING;
    return NN2_REPLY_OK;
  case NN2_CMD_STOP:
    if (c->state != NN2_COMM_IDLE && c->state != NN2_COMM_STREAMING &&
        c->state != NN2_COMM_STARTING && c->state != NN2_COMM_STOPPING)
      return NN2_REPLY_STATE;
    if (off(c) != NN2_OK)
      return NN2_REPLY_SAFETY;
    if (c->optical.state && c->optical.state(c->optical.ctx) != NN2_OPTICAL_IDLE)
      c->state = NN2_COMM_STOPPING;
    else
      end_session(c, NN2_COMM_IDLE);
    return NN2_REPLY_OK;
  case NN2_CMD_SAFE_OFF:
    if (off(c) != NN2_OK)
      return NN2_REPLY_SAFETY;
    end_session(c, NN2_COMM_SAFE_OFF);
    return NN2_REPLY_OK;
  case NN2_CMD_CLEAR_FAULT:
    if (c->state != NN2_COMM_FAULT && c->state != NN2_COMM_SAFE_OFF)
      return NN2_REPLY_STATE;
    if (c->fault == FAULT_CLOCK)
      return NN2_REPLY_PRECONDITION;
    if (off(c) != NN2_OK)
      return NN2_REPLY_SAFETY;
    if (c->optical.clear_fault &&
        c->optical.clear_fault(c->optical.ctx) != NN2_OK)
      return NN2_REPLY_PRECONDITION;
    c->fault = 0;
    end_session(c, NN2_COMM_IDLE);
    return NN2_REPLY_OK;
  case NN2_CMD_SET_FS:
    if (!nn2_core_optical_fs_valid(arg[0]) || (c->optical.real_samples &&
        !(arg[0]==2 || arg[0]==4 || arg[0]==5 || arg[0]==8 || arg[0]==10)))
      return NN2_REPLY_ARGUMENT;
    if (c->state != NN2_COMM_IDLE)
      return NN2_REPLY_STATE;
    c->fs = arg[0];
    return NN2_REPLY_OK;
  case NN2_CMD_SET_LED_CURRENT:
    if (arg[0] >= 20 || nn2_core_get_u16(arg + 1) >
        (c->optical.state ? c->optical.current_limit_ma : 50))
      return NN2_REPLY_ARGUMENT;
    if (c->state != NN2_COMM_IDLE)
      return NN2_REPLY_STATE;
    c->current_ma[arg[0]] = nn2_core_get_u16(arg + 1);
    return NN2_REPLY_OK;
  default:
    return NN2_REPLY_OK;
  }
}
size_t nn2_app_comm_process(nn2_app_comm_storage_t *s, uint64_t peer,
                            uint64_t now, const uint8_t *input, size_t length,
                            uint8_t *output, size_t capacity) {
  comm_t c;
  nn2_core_packet_t req;
  if (!peer || !output || capacity < REPLY_MAX || !load(s, &c))
    return 0;
  if (!nn2_core_packet_decode(input, length, &req) ||
      req.type != NN2_MSG_REQUEST || req.length < 6) {
    ++c.malformed;
    save(s, &c);
    return 0;
  }
  uint32_t id = nn2_core_get_u32(req.payload);
  uint16_t op = nn2_core_get_u16(req.payload + 4),
           argc = (uint16_t)(req.length - 6u);
  const uint8_t *arg = req.payload + 6;
  const nn2_core_command_t *command = nn2_core_command(op);
  uint16_t error =
      tick(&c, now, true) == NN2_OK ? NN2_REPLY_OK : NN2_REPLY_SAFETY;
  if (req.flags & UINT16_C(0xfffe)) {
    ++c.reserved;
    error = NN2_REPLY_RESERVED_FLAGS;
  } else if (req.protocol_version != NN2_CORE_PROTOCOL_VERSION)
    error = NN2_REPLY_BAD_FRAME;
  else if (!command)
    error = NN2_REPLY_OPCODE;
  else if (argc != command->argument_size)
    error = NN2_REPLY_ARGUMENT;
  bool conflict = false;
  bool cacheable = !error && command && command->cached;
  if (cacheable && argc > sizeof(c.cache[0].args)) {
    error = NN2_REPLY_PRECONDITION;
    cacheable = false;
  }
  if (cacheable) {
    for (unsigned i = 0; i < NN2_APP_REPLY_CACHE_SIZE; i++) {
      cache_t *e = &c.cache[i];
      if (e->reply_size && e->peer == peer && e->id == id) {
        if (e->opcode == op && e->size == argc &&
            e->session == req.session_id && !memcmp(e->args, arg, argc)) {
          memcpy(output, e->reply, e->reply_size);
          ++c.duplicates;
          /* A valid owner retry is activity but cannot repeat effects. */
          if (c.owner == peer &&
              nn2_core_get_u16(e->reply + 34) == NN2_REPLY_OK &&
              nn2_core_get_u32(e->reply + 12) == c.session &&
              (!req.session_id || req.session_id == c.session)) {
            c.anchor = now;
            c.grace = false;
          }
          save(s, &c);
          return e->reply_size;
        }
        error = NN2_REPLY_ARGUMENT;
        conflict = true;
        break;
      }
    }
  }
  /* Exact cached acknowledgements must survive session teardown (lost STOP
   * reply). Only fresh effects pass through current-session/owner validation.
   * Replaying an old session's ACK never refreshes a new session's liveness. */
  if (!error && op != NN2_CMD_SAFE_OFF && req.session_id &&
      req.session_id != c.session)
    error = NN2_REPLY_STATE;
  if (!error && c.owner && c.owner != peer && op != NN2_CMD_INFO &&
      op != NN2_CMD_GET_CONFIG && op != NN2_CMD_GET_TIME &&
      op != NN2_CMD_SAFE_OFF)
    error = NN2_REPLY_STATE;
  if (!error && cacheable && !fresh_mutation(&c, peer, id))
    error = NN2_REPLY_ARGUMENT;
  if (error)
    cacheable = false;
  if (!error)
    error = execute(&c, peer, now, op, arg);
  if (!error && c.owner == peer) {
    c.anchor = now;
    c.grace = false;
  }
  nn2_core_packet_t reply = {.type = NN2_MSG_REPLY,
                             .protocol_version = 1,
                             .flags = c.optical.real_samples ? 0 : NN2_FLAG_SIMULATED,
                             .session_id = c.session,
                             .sequence = ++c.sequence,
                             .timestamp_us = now,
                             .length = 28};
  nn2_core_put_u32(reply.payload, id);
  nn2_core_put_u16(reply.payload + 4, op);
  nn2_core_put_u16(reply.payload + 6, error);
  uint8_t *body = reply.payload + 8;
  body[0] = c.state;
  nn2_core_put_u16(body + 1, c.fault);
  body[3] = c.optical.real_samples ? 0 : 1;
  nn2_core_put_u64(body + 4, c.boot);
  nn2_core_put_u64(body + 12, now);
  if (!error && op == NN2_CMD_INFO) {
    const char *info = c.optical.state ?
        (c.optical.policy_info ? c.optical.policy_info :
        "NoctiNIRS2/0.1.0;wire=ADR0004/1;SIMULATED;led=REAL_BRINGUP;short_policy=STRICT;frames=unsupported;capture=unsupported") :
        "NoctiNIRS2/0.1.0;wire=ADR0004/1;SIMULATED;frames=unsupported;capture=unsupported";
    size_t n = strlen(info);
    memcpy(body + 20, info, n);
    reply.length += (uint16_t)n;
  } else if (!error && op == NN2_CMD_GET_CONFIG) {
    body[20] = c.fs;
    body[21] = 20;
    for (unsigned i = 0; i < 20; i++)
      nn2_core_put_u16(body + 22 + i * 2u, c.current_ma[i]);
    reply.length += 42;
  } else if (!error && (op == NN2_CMD_GET_TIME || op == NN2_CMD_HEARTBEAT ||
                        op == NN2_CMD_STATUS)) {
    memcpy(body + 20, arg, 8);
    nn2_core_put_u64(body + 28, now);
    nn2_core_put_u64(body + 36, now);
    reply.length += 24;
    if (op == NN2_CMD_STATUS) {
      nn2_core_put_u32(body + 44, c.requests);
      nn2_core_put_u32(body + 48, c.duplicates);
      nn2_core_put_u32(body + 52, c.expired);
      nn2_core_put_u32(body + 56, c.malformed);
      nn2_core_put_u32(body + 60, c.reserved);
      body[64] = c.grace ? 1u : 0u;
      reply.length += 21;
    }
  }
  size_t count = nn2_core_packet_encode(&reply, output, capacity);
  if (!conflict && cacheable && argc <= 3 && count <= REPLY_MAX) {
    cache_t *e = &c.cache[c.next];
    *e = (cache_t){.peer = peer,
                   .id = id,
                   .session = req.session_id,
                   .opcode = op,
                   .size = argc,
                   .reply_size = (uint16_t)count};
    memcpy(e->args, arg, argc);
    memcpy(e->reply, output, count);
    c.next = (uint8_t)((c.next + 1u) % NN2_APP_REPLY_CACHE_SIZE);
  }
  ++c.requests;
  save(s, &c);
  return count;
}
void nn2_app_comm_stamp_tx(uint8_t *reply, size_t length, uint64_t now) {
  if (!reply || length < 84 || memcmp(reply, "NNS2", 4) ||
      reply[5] != NN2_MSG_REPLY || nn2_core_get_u16(reply + 34) != NN2_REPLY_OK)
    return;
  uint16_t op = nn2_core_get_u16(reply + 32);
  if (op != NN2_CMD_GET_TIME && op != NN2_CMD_HEARTBEAT && op != NN2_CMD_STATUS)
    return;
  nn2_core_put_u64(reply + 20, now);
  nn2_core_put_u64(reply + 72, now);
  nn2_core_put_u32(reply + length - 4, nn2_core_crc32(reply, length - 4));
}

/* Same Application/network caller as command processing; FRAME sequence shares
 * envelope sequence, while ACQ1 has a separate optical-frame counter. */
size_t nn2_app_comm_frame(nn2_app_comm_storage_t *s, uint64_t *peer,
                         uint8_t *output, size_t capacity)
{
    comm_t c; nn2_core_packet_t packet={.type=NN2_MSG_FRAME,
        .protocol_version=1,.flags=NN2_FLAG_TS_ESTIMATED};
    if (!peer || !output || capacity<NN2_CORE_PACKET_MAX || !load(s,&c) ||
        c.state!=NN2_COMM_STREAMING || !c.owner || !c.optical.read_frame)
        return 0;
    if (c.optical.read_frame(c.optical.ctx,packet.payload,sizeof(packet.payload),
        &packet.timestamp_us)!=NN2_OK) return 0;
    packet.length=NN2_ACQ_PAYLOAD_SIZE; /* ACQ1: versioned local binding. */
    packet.session_id=c.session; packet.sequence=++c.sequence;
    *peer=c.owner;
    size_t count=nn2_core_packet_encode(&packet,output,capacity);
    save(s,&c); return count;
}
