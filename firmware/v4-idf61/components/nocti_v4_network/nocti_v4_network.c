#include "nocti_v4_network.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "nocti_v4_common.h"
#include "nocti_v4_ads_config.h"
#include "nocti_v4_scheduler_config.h"

#define NETWORK_RING_MASK (NOCTI_V4_NETWORK_FRAME_RING_CAPACITY - 1u)
#define NETWORK_POLL_MS 20u
#define NETWORK_MAX_COMMAND_BYTES 64u
#define NETWORK_MAX_REPLY_BYTES 256u
#define STREAM_MAGIC 0xA5u
#define STREAM_FORMAT_VERSION 1u
#define STREAM_FLAG_CRC16 0x01u
#define STREAM_HEADER_BYTES 12u
#define STREAM_CRC_BYTES 2u
#define STREAM_MAX_DATAGRAM_BYTES 1280u
#define STREAM_TIME64_PERIOD_US 10000000ULL

enum {
    REC_TIME64 = 3,
    REC_FNIRS_CFG = 6,
    REC_FNIRS_FRAME = 7,
    REC_SESSION = 8,
    REC_V4_OPTICAL_FRAME = 11,
};

enum {
    FNIRS_FRAME_F_PARTIAL = 0x01,
    FNIRS_FRAME_F_LED_OFF = 0x02,
    FNIRS_FRAME_F_PAIRED = 0x04,
};

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint16_t sequence;
    uint32_t relative_timestamp_us;
    uint16_t payload_length;
} stream_header_v1_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t reserved[3];
    uint32_t session_id;
    uint64_t t0_us;
} session_payload_v1_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t map_mode;
    uint8_t frame_fs_hz;
    uint8_t slot_count;
    uint8_t k_on;
    uint8_t k_off;
    uint8_t current_735_mA;
    uint8_t current_850_mA;
    uint16_t on_settle_us;
    uint16_t on_window_us;
    uint16_t off_min_settle_us;
    uint16_t slot_period_us;
} fnirs_cfg_v2_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t flags;
    uint16_t frame_id;
    uint32_t valid_mask;
    uint32_t underk_mask;
    uint32_t crc_mask;
    int32_t avg[NOCTI_V4_SLOT_COUNT][NOCTI_V4_ADS_CHANNEL_COUNT];
    uint8_t n_used[NOCTI_V4_SLOT_COUNT];
} fnirs_frame_v2_t;

typedef struct __attribute__((packed)) {
    uint8_t slot_index;
    uint8_t logical_source;
    uint8_t pca_channel;
    uint8_t wavelength_code; /* 0=735 nm, 1=850 nm */
    uint8_t n_on;
    uint8_t n_off;
    uint8_t quality;
    uint16_t reason;
    int32_t i_on[NOCTI_V4_ADS_CHANNEL_COUNT];
    int32_t i_led_off[NOCTI_V4_ADS_CHANNEL_COUNT];
    int32_t i_corrected[NOCTI_V4_ADS_CHANNEL_COUNT];
} v4_slot_wire_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t frame_quality;
    uint8_t target_valid_slots;
    uint8_t minimum_valid_slots;
    uint8_t invalid_slots;
    uint8_t slot_count;
    uint16_t reserved;
    uint32_t session_epoch;
    uint32_t frame_sequence;
    uint64_t nominal_frame_timestamp_us;
    uint64_t completed_timestamp_us;
    uint32_t frame_reason;
    v4_slot_wire_t slot[NOCTI_V4_SLOT_COUNT];
} v4_optical_frame_wire_t;

typedef struct {
    nocti_v4_network_t *public_network;
    nocti_v4_time_t *timebase;
    nocti_v4_health_t *health;
    volatile nocti_v4_network_lifecycle_t lifecycle;
    volatile bool ap_started;
    volatile uint32_t station_count;
    volatile bool station_has_connected;
    volatile bool peer_valid;
    volatile uint32_t peer_addr_network_order;
    volatile bool stream_enabled;
    volatile bool legacy_compat_enabled;
    volatile bool scan_session_accepting;
    volatile uint32_t scan_session_epoch;
    volatile esp_err_t last_error;
    int socket_fd;
    TaskHandle_t task;
    StaticSemaphore_t ring_empty_storage;
    SemaphoreHandle_t ring_empty;
    volatile uint32_t ring_head;
    volatile uint32_t ring_tail;
    volatile bool transmit_busy;
    nocti_v4_completed_optical_frame_t ring[NOCTI_V4_NETWORK_FRAME_RING_CAPACITY];
    uint16_t record_sequence;
    uint32_t encoded_session;
    uint64_t encoder_t0_us;
    uint64_t next_time64_due_us;
    volatile bool metadata_pending;
    uint8_t datagram[STREAM_MAX_DATAGRAM_BYTES];
    fnirs_frame_v2_t legacy_payload;
    v4_optical_frame_wire_t v4_payload;
    char reply[NETWORK_MAX_REPLY_BYTES];
    esp_event_handler_instance_t wifi_event_instance;
} network_runtime_t;

static network_runtime_t s_runtime;
static bool s_runtime_claimed;

_Static_assert((NOCTI_V4_NETWORK_FRAME_RING_CAPACITY &
                (NOCTI_V4_NETWORK_FRAME_RING_CAPACITY - 1u)) == 0u,
               "network frame ring must be a power of two");
_Static_assert(sizeof(stream_header_v1_t) == STREAM_HEADER_BYTES,
               "stream-v1 header compatibility changed");
_Static_assert(sizeof(fnirs_cfg_v2_t) == 16u,
               "legacy fNIRS configuration payload changed");
_Static_assert(sizeof(fnirs_frame_v2_t) == 356u,
               "legacy paired-frame payload changed");
_Static_assert(sizeof(v4_slot_wire_t) == 57u,
               "V4 optical slot wire ABI changed");
_Static_assert(sizeof(v4_optical_frame_wire_t) == 1176u,
               "V4 optical-frame wire ABI changed");
_Static_assert(sizeof(v4_optical_frame_wire_t) + STREAM_HEADER_BYTES +
               STREAM_CRC_BYTES <= STREAM_MAX_DATAGRAM_BYTES,
               "V4 quality record exceeds bounded UDP datagram");
_Static_assert(NOCTI_V4_ADS_CHANNEL_COUNT == 4u,
               "network protocol is explicitly four-detector");

static inline void add(uint32_t *value, uint32_t amount)
{
    (void)__atomic_fetch_add(value, amount, __ATOMIC_RELAXED);
}

static inline void store(uint32_t *value, uint32_t amount)
{
    __atomic_store_n(value, amount, __ATOMIC_RELEASE);
}

static uint32_t ring_depth(const network_runtime_t *r)
{
    const uint32_t head = __atomic_load_n(&r->ring_head, __ATOMIC_ACQUIRE);
    const uint32_t tail = __atomic_load_n(&r->ring_tail, __ATOMIC_ACQUIRE);
    return head - tail;
}

static void update_heap_health(network_runtime_t *r)
{
    const uint32_t free_internal = (uint32_t)heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    store(&r->health->minimum_free_heap,
          (uint32_t)heap_caps_get_minimum_free_size(
              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    store(&r->health->largest_free_block, largest);
    (void)free_internal;
}

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static size_t build_record(network_runtime_t *r, uint8_t type,
                           uint64_t timestamp_us, const void *payload,
                           size_t payload_length)
{
    const size_t total = STREAM_HEADER_BYTES + payload_length + STREAM_CRC_BYTES;
    if (payload == NULL || total > sizeof(r->datagram)) return 0u;
    const stream_header_v1_t header = {
        .magic = STREAM_MAGIC,
        .version = STREAM_FORMAT_VERSION,
        .type = type,
        .flags = STREAM_FLAG_CRC16,
        .sequence = r->record_sequence++,
        .relative_timestamp_us = (uint32_t)(timestamp_us - r->encoder_t0_us),
        .payload_length = (uint16_t)payload_length,
    };
    memcpy(r->datagram, &header, sizeof(header));
    memcpy(r->datagram + sizeof(header), payload, payload_length);
    const uint16_t crc = crc16_ccitt_false(r->datagram,
                                            sizeof(header) + payload_length);
    r->datagram[sizeof(header) + payload_length] = (uint8_t)(crc >> 8);
    r->datagram[sizeof(header) + payload_length + 1u] = (uint8_t)crc;
    return total;
}

static bool peer_destination(const network_runtime_t *r,
                             struct sockaddr_in *destination)
{
    if (!__atomic_load_n(&r->peer_valid, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&r->stream_enabled, __ATOMIC_ACQUIRE)) return false;
    memset(destination, 0, sizeof(*destination));
    destination->sin_family = AF_INET;
    destination->sin_port = htons(NOCTI_V4_UDP_STREAM_PORT);
    destination->sin_addr.s_addr = __atomic_load_n(&r->peer_addr_network_order,
                                                    __ATOMIC_ACQUIRE);
    return true;
}

static bool send_record(network_runtime_t *r, const struct sockaddr_in *destination,
                        uint8_t type, uint64_t timestamp_us, const void *payload,
                        size_t payload_length)
{
    const size_t bytes = build_record(r, type, timestamp_us, payload,
                                      payload_length);
    if (bytes == 0u) {
        add(&r->health->udp_send_errors, 1u);
        return false;
    }
    const int sent = sendto(r->socket_fd, r->datagram, bytes, MSG_DONTWAIT,
                            (const struct sockaddr *)destination,
                            sizeof(*destination));
    if (sent != (int)bytes) {
        add(&r->health->udp_send_errors, 1u);
        r->last_error = ESP_FAIL;
        return false;
    }
    add(&r->health->udp_bytes_sent, (uint32_t)bytes);
    return true;
}

static bool send_metadata(network_runtime_t *r,
                          const struct sockaddr_in *destination,
                          const nocti_v4_completed_optical_frame_t *frame)
{
    r->record_sequence = 0u;
    r->encoded_session = frame->session_epoch;
    r->encoder_t0_us = frame->nominal_frame_timestamp_us;
    r->next_time64_due_us = frame->nominal_frame_timestamp_us +
                            STREAM_TIME64_PERIOD_US;
    const session_payload_v1_t session = {
        .version = 1u,
        .session_id = frame->session_epoch,
        .t0_us = frame->nominal_frame_timestamp_us,
    };
    if (!send_record(r, destination, REC_SESSION,
                     frame->nominal_frame_timestamp_us, &session,
                     sizeof(session))) return false;
    uint8_t time64[8];
    for (uint8_t i = 0u; i < 8u; ++i) {
        time64[i] = (uint8_t)(frame->nominal_frame_timestamp_us >>
                              (56u - (uint8_t)(8u * i)));
    }
    if (!send_record(r, destination, REC_TIME64,
                     frame->nominal_frame_timestamp_us, time64,
                     sizeof(time64))) return false;
    const fnirs_cfg_v2_t config = {
        .version = 2u,
        .map_mode = 0u,
        .frame_fs_hz = 10u,
        .slot_count = NOCTI_V4_SLOT_COUNT,
        .k_on = NOCTI_V4_K_ON_TARGET,
        .k_off = NOCTI_V4_K_OFF_TARGET,
        .current_735_mA = 15u,
        .current_850_mA = 15u,
        .on_settle_us = NOCTI_V4_LED_ON_SETTLE_US,
        .on_window_us = NOCTI_V4_K_ON_TARGET * NOCTI_V4_ADS_EXPECTED_PERIOD_US,
        .off_min_settle_us = NOCTI_V4_LED_OFF_SETTLE_US,
        .slot_period_us = NOCTI_V4_SLOT_PERIOD_US,
    };
    return send_record(r, destination, REC_FNIRS_CFG,
                       frame->nominal_frame_timestamp_us, &config,
                       sizeof(config));
}

static void build_legacy_payload(const nocti_v4_completed_optical_frame_t *frame,
                                 bool led_off, fnirs_frame_v2_t *payload)
{
    memset(payload, 0, sizeof(*payload));
    payload->version = 2u;
    payload->flags = FNIRS_FRAME_F_PAIRED |
                     (led_off ? FNIRS_FRAME_F_LED_OFF : 0u);
    payload->frame_id = (uint16_t)frame->frame_sequence;
    if (frame->quality != NOCTI_V4_FRAME_QUALITY_TARGET_VALID) {
        payload->flags |= FNIRS_FRAME_F_PARTIAL;
    }
    for (uint8_t slot = 0u; slot < NOCTI_V4_SLOT_COUNT; ++slot) {
        const nocti_v4_optical_slot_value_t *value = &frame->slot[slot];
        const uint8_t used = led_off ? value->n_off_used : value->n_on_used;
        payload->n_used[slot] = used;
        if (used >= NOCTI_V4_K_ON_MINIMUM) payload->valid_mask |= 1u << slot;
        if (used < NOCTI_V4_K_ON_TARGET) payload->underk_mask |= 1u << slot;
        for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT;
             ++channel) {
            payload->avg[slot][channel] = led_off ?
                value->i_led_off[channel] : value->i_on[channel];
        }
    }
}

static bool build_v4_payload(network_runtime_t *r,
                             const nocti_v4_completed_optical_frame_t *frame)
{
    memset(&r->v4_payload, 0, sizeof(r->v4_payload));
    r->v4_payload.version = 1u;
    r->v4_payload.frame_quality = (uint8_t)frame->quality;
    r->v4_payload.target_valid_slots = frame->target_valid_slots;
    r->v4_payload.minimum_valid_slots = frame->minimum_valid_slots;
    r->v4_payload.invalid_slots = frame->invalid_slots;
    r->v4_payload.slot_count = NOCTI_V4_SLOT_COUNT;
    r->v4_payload.session_epoch = frame->session_epoch;
    r->v4_payload.frame_sequence = frame->frame_sequence;
    r->v4_payload.nominal_frame_timestamp_us = frame->nominal_frame_timestamp_us;
    r->v4_payload.completed_timestamp_us = frame->completed_timestamp_us;
    r->v4_payload.frame_reason = frame->reason;
    for (uint8_t slot = 0u; slot < NOCTI_V4_SLOT_COUNT; ++slot) {
        const nocti_v4_optical_slot_value_t *source = &frame->slot[slot];
        v4_slot_wire_t *target = &r->v4_payload.slot[slot];
        target->slot_index = source->slot_index;
        target->logical_source = source->logical_source;
        target->pca_channel = source->pca_channel;
        target->wavelength_code = source->wavelength_nm == 850u ? 1u : 0u;
        target->n_on = source->n_on_available;
        target->n_off = source->n_off_available;
        target->quality = (uint8_t)source->quality;
        target->reason = (uint16_t)source->reason;
        for (uint8_t channel = 0u; channel < NOCTI_V4_ADS_CHANNEL_COUNT;
             ++channel) {
            const int64_t corrected = source->i_corrected[channel];
            if (corrected < INT32_MIN || corrected > INT32_MAX) {
                add(&r->health->numeric_overflow_errors, 1u);
                return false;
            }
            target->i_on[channel] = source->i_on[channel];
            target->i_led_off[channel] = source->i_led_off[channel];
            target->i_corrected[channel] = (int32_t)corrected;
        }
    }
    return true;
}

static bool send_frame(network_runtime_t *r,
                       const nocti_v4_completed_optical_frame_t *frame)
{
    struct sockaddr_in destination;
    if (!peer_destination(r, &destination)) return false;
    if (__atomic_load_n(&r->metadata_pending, __ATOMIC_ACQUIRE) ||
        r->encoded_session != frame->session_epoch) {
        if (!send_metadata(r, &destination, frame)) return false;
        __atomic_store_n(&r->metadata_pending, false, __ATOMIC_RELEASE);
    }
    if (__atomic_load_n(&r->legacy_compat_enabled, __ATOMIC_ACQUIRE)) {
        build_legacy_payload(frame, false, &r->legacy_payload);
        if (!send_record(r, &destination, REC_FNIRS_FRAME,
                         frame->completed_timestamp_us, &r->legacy_payload,
                         sizeof(r->legacy_payload))) return false;
        build_legacy_payload(frame, true, &r->legacy_payload);
        if (!send_record(r, &destination, REC_FNIRS_FRAME,
                         frame->completed_timestamp_us, &r->legacy_payload,
                         sizeof(r->legacy_payload))) return false;
    }
    if (!build_v4_payload(r, frame) ||
        !send_record(r, &destination, REC_V4_OPTICAL_FRAME,
                     frame->completed_timestamp_us, &r->v4_payload,
                     sizeof(r->v4_payload))) return false;
    if (frame->completed_timestamp_us >= r->next_time64_due_us) {
        uint8_t time64[8];
        for (uint8_t i = 0u; i < 8u; ++i) {
            time64[i] = (uint8_t)(frame->completed_timestamp_us >>
                                  (56u - (uint8_t)(8u * i)));
        }
        if (!send_record(r, &destination, REC_TIME64,
                         frame->completed_timestamp_us, time64,
                         sizeof(time64))) return false;
        r->next_time64_due_us += STREAM_TIME64_PERIOD_US;
    }
    return true;
}

static const nocti_v4_completed_optical_frame_t *ring_front(
    const network_runtime_t *r)
{
    const uint32_t tail = __atomic_load_n(&r->ring_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&r->ring_head, __ATOMIC_ACQUIRE);
    return tail == head ? NULL : &r->ring[tail & NETWORK_RING_MASK];
}

static void ring_consume(network_runtime_t *r)
{
    const uint32_t tail = __atomic_load_n(&r->ring_tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&r->ring_head, __ATOMIC_ACQUIRE);
    __atomic_store_n(&r->ring_tail, tail + 1u, __ATOMIC_RELEASE);
    store(&r->health->network_frame_ring_depth, head - (tail + 1u));
}

static bool command_is(const uint8_t *data, size_t length, const char *expected)
{
    const size_t expected_length = strlen(expected);
    while (length != 0u && (data[length - 1u] == '\r' ||
           data[length - 1u] == '\n' || data[length - 1u] == ' ')) --length;
    if (length != expected_length) return false;
    for (size_t i = 0u; i < length; ++i) {
        char c = (char)data[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c != expected[i]) return false;
    }
    return true;
}

static void send_control_reply(network_runtime_t *r,
                               const struct sockaddr_in *source,
                               socklen_t source_length, const char *reply)
{
    const size_t length = strlen(reply);
    (void)sendto(r->socket_fd, reply, length, MSG_DONTWAIT,
                 (const struct sockaddr *)source, source_length);
}

static void process_control(network_runtime_t *r)
{
    uint8_t command[NETWORK_MAX_COMMAND_BYTES];
    for (uint8_t attempt = 0u; attempt < 4u; ++attempt) {
        struct sockaddr_in source;
        socklen_t source_length = sizeof(source);
        const int received = recvfrom(r->socket_fd, command, sizeof(command),
                                      MSG_DONTWAIT, (struct sockaddr *)&source,
                                      &source_length);
        if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) r->last_error = ESP_FAIL;
            return;
        }
        if (received == 0) return;
        if (command_is(command, (size_t)received, "PING")) {
            send_control_reply(r, &source, source_length,
                               "PONG firmware=NoctiNIRS-V4 protocol=stream-v1+v4-quality\n");
        } else if (command_is(command, (size_t)received, "HELLO")) {
            __atomic_store_n(&r->peer_addr_network_order,
                             source.sin_addr.s_addr, __ATOMIC_RELEASE);
            __atomic_store_n(&r->peer_valid, true, __ATOMIC_RELEASE);
            __atomic_store_n(&r->metadata_pending, true, __ATOMIC_RELEASE);
            send_control_reply(r, &source, source_length,
                               "NOCTI HELLO OK ctrl=3333 stream=3334 control=stream_only\n");
        } else if (command_is(command, (size_t)received, "INFO")) {
            send_control_reply(r, &source, source_length,
                               "NOCTI V4 phase=V4-7 frame_hz=10 slots=20 default=quality_v1 optional=legacy_paired_v2+quality_v1\n");
        } else if (command_is(command, (size_t)received, "STREAM_START")) {
            __atomic_store_n(&r->peer_addr_network_order,
                             source.sin_addr.s_addr, __ATOMIC_RELEASE);
            __atomic_store_n(&r->peer_valid, true, __ATOMIC_RELEASE);
            __atomic_store_n(&r->legacy_compat_enabled, false,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&r->stream_enabled, true, __ATOMIC_RELEASE);
            __atomic_store_n(&r->metadata_pending, true, __ATOMIC_RELEASE);
            send_control_reply(r, &source, source_length,
                               "STREAM_START OK profile=V4_ONLY destination_port=3334 acquisition_control=SERIAL_ONLY\n");
        } else if (command_is(command, (size_t)received,
                              "STREAM_START_COMPAT")) {
            __atomic_store_n(&r->peer_addr_network_order,
                             source.sin_addr.s_addr, __ATOMIC_RELEASE);
            __atomic_store_n(&r->peer_valid, true, __ATOMIC_RELEASE);
            __atomic_store_n(&r->legacy_compat_enabled, true,
                             __ATOMIC_RELEASE);
            __atomic_store_n(&r->stream_enabled, true, __ATOMIC_RELEASE);
            __atomic_store_n(&r->metadata_pending, true, __ATOMIC_RELEASE);
            send_control_reply(r, &source, source_length,
                               "STREAM_START_COMPAT OK profile=V3_PAIRED_PLUS_V4 destination_port=3334 acquisition_control=SERIAL_ONLY\n");
        } else if (command_is(command, (size_t)received, "STREAM_STOP")) {
            __atomic_store_n(&r->stream_enabled, false, __ATOMIC_RELEASE);
            send_control_reply(r, &source, source_length, "STREAM_STOP OK\n");
        } else {
            send_control_reply(r, &source, source_length,
                               "ERR UNKNOWN allowed=PING,HELLO,INFO,STREAM_START,STREAM_START_COMPAT,STREAM_STOP\n");
        }
    }
}

static void network_task(void *argument)
{
    network_runtime_t *r = (network_runtime_t *)argument;
    __atomic_store_n(&r->health->network_task_core, (int32_t)xPortGetCoreID(),
                     __ATOMIC_RELEASE);
    store(&r->health->network_task_priority, NOCTI_V4_NETWORK_TASK_PRIORITY);
    TickType_t last_watermark = xTaskGetTickCount();
    for (;;) {
        process_control(r);
        for (;;) {
            /* Publish ownership before inspecting the ring so SCAN_STOP
             * cannot freeze a session while a record is being transmitted.
             * The tail advances only after send_frame returns; the producer
             * therefore cannot overwrite this immutable record and no extra
             * 2,840-byte transmission copy is needed. */
            __atomic_store_n(&r->transmit_busy, true, __ATOMIC_RELEASE);
            const nocti_v4_completed_optical_frame_t *frame = ring_front(r);
            if (frame == NULL) {
                __atomic_store_n(&r->transmit_busy, false, __ATOMIC_RELEASE);
                break;
            }
            if (send_frame(r, frame)) {
                add(&r->health->udp_frames_sent, 1u);
            } else {
                add(&r->health->udp_frames_dropped, 1u);
            }
            ring_consume(r);
        }
        if (ring_depth(r) == 0u &&
            !__atomic_load_n(&r->transmit_busy, __ATOMIC_ACQUIRE)) {
            (void)xSemaphoreGive(r->ring_empty);
        }
        const TickType_t now = xTaskGetTickCount();
        if ((now - last_watermark) >= pdMS_TO_TICKS(1000u)) {
            store(&r->health->network_task_stack_watermark_bytes,
                  (uint32_t)uxTaskGetStackHighWaterMark(NULL));
            update_heap_health(r);
            last_watermark = now;
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NETWORK_POLL_MS));
    }
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)event_data;
    network_runtime_t *r = (network_runtime_t *)argument;
    if (event_base != WIFI_EVENT || r == NULL) return;
    if (event_id == WIFI_EVENT_AP_START) {
        __atomic_store_n(&r->ap_started, true, __ATOMIC_RELEASE);
        __atomic_store_n(&r->lifecycle, NOCTI_V4_NETWORK_READY, __ATOMIC_RELEASE);
        store(&r->health->wifi_init_ok, 1u);
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        add(&r->health->wifi_connect_count, 1u);
        if (__atomic_exchange_n(&r->station_has_connected, true,
                                __ATOMIC_ACQ_REL)) {
            add(&r->health->wifi_reconnect_count, 1u);
        }
        (void)__atomic_add_fetch(&r->station_count, 1u, __ATOMIC_ACQ_REL);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        add(&r->health->wifi_disconnect_count, 1u);
        uint32_t stations = __atomic_load_n(&r->station_count, __ATOMIC_ACQUIRE);
        while (stations != 0u &&
               !__atomic_compare_exchange_n(&r->station_count, &stations,
                    stations - 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
        __atomic_store_n(&r->peer_valid, false, __ATOMIC_RELEASE);
        __atomic_store_n(&r->stream_enabled, false, __ATOMIC_RELEASE);
    } else if (event_id == WIFI_EVENT_AP_STOP) {
        __atomic_store_n(&r->ap_started, false, __ATOMIC_RELEASE);
        __atomic_store_n(&r->lifecycle, NOCTI_V4_NETWORK_STOPPED, __ATOMIC_RELEASE);
    }
}

static esp_err_t initialize_softap(network_runtime_t *r)
{
    esp_err_t result = nvs_flash_init();
    /* Report NVS errors; never erase existing board provisioning automatically. */
    if (result != ESP_OK) return result;
    result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    if (esp_netif_create_default_wifi_ap() == NULL) return ESP_ERR_NO_MEM;
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) return result;
    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  wifi_event_handler, r,
                                                  &r->wifi_event_instance);
    if (result != ESP_OK) return result;
    wifi_config_t config;
    memset(&config, 0, sizeof(config));
    memcpy(config.ap.ssid, NOCTI_V4_WIFI_AP_SSID,
           sizeof(NOCTI_V4_WIFI_AP_SSID) - 1u);
    config.ap.ssid_len = sizeof(NOCTI_V4_WIFI_AP_SSID) - 1u;
    memcpy(config.ap.password, NOCTI_V4_WIFI_AP_PASSWORD,
           sizeof(NOCTI_V4_WIFI_AP_PASSWORD) - 1u);
    config.ap.channel = NOCTI_V4_WIFI_AP_CHANNEL;
    config.ap.max_connection = NOCTI_V4_WIFI_AP_MAX_CONNECTIONS;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (result == ESP_OK) result = esp_wifi_start();
    return result;
}

static esp_err_t initialize_socket(network_runtime_t *r)
{
    r->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (r->socket_fd < 0) {
        add(&r->health->udp_socket_create_errors, 1u);
        return ESP_FAIL;
    }
    const int flags = fcntl(r->socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(r->socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(r->socket_fd);
        r->socket_fd = -1;
        add(&r->health->udp_socket_create_errors, 1u);
        return ESP_FAIL;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(NOCTI_V4_UDP_CONTROL_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(r->socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(r->socket_fd);
        r->socket_fd = -1;
        add(&r->health->udp_socket_create_errors, 1u);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t nocti_v4_network_init(nocti_v4_network_t *network,
                                nocti_v4_time_t *timebase,
                                nocti_v4_health_t *health)
{
    if (network == NULL || timebase == NULL || health == NULL ||
        s_runtime_claimed) return ESP_ERR_INVALID_ARG;
    memset(network, 0, sizeof(*network));
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime_claimed = true;
    network_runtime_t *r = &s_runtime;
    r->public_network = network;
    r->timebase = timebase;
    r->health = health;
    r->socket_fd = -1;
    r->lifecycle = NOCTI_V4_NETWORK_STARTING;
    r->last_error = ESP_OK;
    network->runtime = r;
    store(&health->free_heap_before_wifi,
          (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    r->ring_empty = xSemaphoreCreateBinaryStatic(&r->ring_empty_storage);
    if (r->ring_empty == NULL) return ESP_ERR_NO_MEM;
    /* Credentials belong in ignored local sdkconfig, never the historical copy. */
    if (sizeof(NOCTI_V4_WIFI_AP_PASSWORD)-1u < 8u ||
        sizeof(NOCTI_V4_WIFI_AP_PASSWORD)-1u > 63u) {
        r->lifecycle = NOCTI_V4_NETWORK_ERROR;
        r->last_error = ESP_ERR_INVALID_ARG;
        return r->last_error;
    }
    esp_err_t result = initialize_softap(r);
    store(&health->free_heap_after_wifi,
          (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    update_heap_health(r);
    if (result == ESP_OK) result = initialize_socket(r);
    if (result != ESP_OK) {
        r->lifecycle = NOCTI_V4_NETWORK_ERROR;
        r->last_error = result;
        return result;
    }
    if (xTaskCreatePinnedToCore(
            network_task, "nocti_v4_network",
            NOCTI_V4_NETWORK_TASK_STACK_BYTES, r,
            NOCTI_V4_NETWORK_TASK_PRIORITY, &r->task,
            NOCTI_V4_NETWORK_TASK_CORE) != pdPASS) {
        r->lifecycle = NOCTI_V4_NETWORK_ERROR;
        r->last_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    store(&health->free_heap_after_wifi,
          (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    network->initialized = true;
    store(&health->wifi_init_ok, 1u);
    return ESP_OK;
}

bool nocti_v4_network_frame_sink(
    void *context, const nocti_v4_completed_optical_frame_t *frame)
{
    nocti_v4_network_t *network = (nocti_v4_network_t *)context;
    if (network == NULL || network->runtime == NULL || frame == NULL) return false;
    network_runtime_t *r = (network_runtime_t *)network->runtime;
    if (!__atomic_load_n(&r->scan_session_accepting, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&r->stream_enabled, __ATOMIC_ACQUIRE) ||
        frame->session_epoch != __atomic_load_n(&r->scan_session_epoch,
                                                 __ATOMIC_ACQUIRE)) return true;
    const uint32_t head = __atomic_load_n(&r->ring_head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&r->ring_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= NOCTI_V4_NETWORK_FRAME_RING_CAPACITY) {
        add(&r->health->network_frame_ring_overflow, 1u);
        add(&r->health->udp_frames_dropped, 1u);
        return false;
    }
    r->ring[head & NETWORK_RING_MASK] = *frame;
    __atomic_store_n(&r->ring_head, head + 1u, __ATOMIC_RELEASE);
    const uint32_t depth = head + 1u - tail;
    store(&r->health->network_frame_ring_depth, depth);
    uint32_t maximum = __atomic_load_n(&r->health->network_frame_ring_depth_max,
                                        __ATOMIC_RELAXED);
    while (depth > maximum &&
           !__atomic_compare_exchange_n(&r->health->network_frame_ring_depth_max,
                &maximum, depth, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
    add(&r->health->udp_frames_enqueued, 1u);
    xTaskNotifyGive(r->task);
    return true;
}

void nocti_v4_network_begin_scan_session(nocti_v4_network_t *network,
                                         uint32_t session_epoch)
{
    if (network == NULL || network->runtime == NULL || session_epoch == 0u) return;
    network_runtime_t *r = (network_runtime_t *)network->runtime;
    __atomic_store_n(&r->ring_tail,
                     __atomic_load_n(&r->ring_head, __ATOMIC_ACQUIRE),
                     __ATOMIC_RELEASE);
    store(&r->health->network_frame_ring_depth, 0u);
    __atomic_store_n(&r->metadata_pending, true, __ATOMIC_RELEASE);
    __atomic_store_n(&r->scan_session_epoch, session_epoch, __ATOMIC_RELEASE);
    __atomic_store_n(&r->scan_session_accepting, true, __ATOMIC_RELEASE);
}

esp_err_t nocti_v4_network_end_scan_session(nocti_v4_network_t *network,
                                             uint32_t timeout_ms)
{
    if (network == NULL || network->runtime == NULL) return ESP_ERR_INVALID_STATE;
    network_runtime_t *r = (network_runtime_t *)network->runtime;
    __atomic_store_n(&r->scan_session_accepting, false, __ATOMIC_RELEASE);
    while (xSemaphoreTake(r->ring_empty, 0u) == pdTRUE) {}
    if (ring_depth(r) == 0u &&
        !__atomic_load_n(&r->transmit_busy, __ATOMIC_ACQUIRE)) return ESP_OK;
    xTaskNotifyGive(r->task);
    return xSemaphoreTake(r->ring_empty, pdMS_TO_TICKS(timeout_ms)) == pdTRUE ?
        ESP_OK : ESP_ERR_TIMEOUT;
}

const char *nocti_v4_network_lifecycle_name(nocti_v4_network_lifecycle_t state)
{
    switch (state) {
        case NOCTI_V4_NETWORK_STOPPED: return "NETWORK_STOPPED";
        case NOCTI_V4_NETWORK_STARTING: return "NETWORK_STARTING";
        case NOCTI_V4_NETWORK_READY: return "NETWORK_READY";
        case NOCTI_V4_NETWORK_ERROR: return "NETWORK_ERROR";
        default: return "NETWORK_UNKNOWN";
    }
}

void nocti_v4_network_get_status(const nocti_v4_network_t *network,
                                 nocti_v4_network_status_t *out_status)
{
    if (out_status == NULL) return;
    memset(out_status, 0, sizeof(*out_status));
    out_status->task_core = -1;
    if (network == NULL || network->runtime == NULL) return;
    const network_runtime_t *r = (const network_runtime_t *)network->runtime;
    out_status->initialized = network->initialized;
    out_status->lifecycle = __atomic_load_n(&r->lifecycle, __ATOMIC_ACQUIRE);
    out_status->ap_started = __atomic_load_n(&r->ap_started, __ATOMIC_ACQUIRE);
    out_status->station_count = __atomic_load_n(&r->station_count, __ATOMIC_ACQUIRE);
    out_status->peer_valid = __atomic_load_n(&r->peer_valid, __ATOMIC_ACQUIRE);
    out_status->peer_ipv4_host_order = ntohl(__atomic_load_n(
        &r->peer_addr_network_order, __ATOMIC_ACQUIRE));
    out_status->stream_enabled = __atomic_load_n(&r->stream_enabled,
                                                  __ATOMIC_ACQUIRE);
    out_status->legacy_compat_enabled = __atomic_load_n(
        &r->legacy_compat_enabled, __ATOMIC_ACQUIRE);
    out_status->scan_session_accepting = __atomic_load_n(
        &r->scan_session_accepting, __ATOMIC_ACQUIRE);
    out_status->scan_session_epoch = __atomic_load_n(&r->scan_session_epoch,
                                                      __ATOMIC_ACQUIRE);
    out_status->control_port = NOCTI_V4_UDP_CONTROL_PORT;
    out_status->stream_port = NOCTI_V4_UDP_STREAM_PORT;
    out_status->ring_depth = ring_depth(r);
    out_status->task_core = (int32_t)__atomic_load_n(&r->health->network_task_core,
                                                     __ATOMIC_ACQUIRE);
    out_status->task_priority = NOCTI_V4_NETWORK_TASK_PRIORITY;
    out_status->task_stack_watermark_bytes = __atomic_load_n(
        &r->health->network_task_stack_watermark_bytes, __ATOMIC_ACQUIRE);
    out_status->last_error = __atomic_load_n(&r->last_error, __ATOMIC_ACQUIRE);
}

void nocti_v4_network_print_status(const nocti_v4_network_t *network)
{
    nocti_v4_network_status_t status;
    nocti_v4_network_get_status(network, &status);
    printf("NETWORK_STATUS initialized=%u state=%s ap_started=%u ssid=%s channel=%u"
           " max_connections=%u stations=%" PRIu32 " ctrl_port=%u stream_port=%u"
           " peer_valid=%u peer=%u.%u.%u.%u stream_enabled=%u profile=%s session_accepting=%u"
           " session=%" PRIu32 " ring_depth=%" PRIu32 "/%u task_core=%" PRId32
           " task_priority=%" PRIu32 " task_stack_watermark=%" PRIu32
           " last_error=%s\n",
           status.initialized ? 1u : 0u,
           nocti_v4_network_lifecycle_name(status.lifecycle),
           status.ap_started ? 1u : 0u, NOCTI_V4_WIFI_AP_SSID,
           NOCTI_V4_WIFI_AP_CHANNEL, NOCTI_V4_WIFI_AP_MAX_CONNECTIONS,
           status.station_count, status.control_port, status.stream_port,
           status.peer_valid ? 1u : 0u,
           (unsigned)((status.peer_ipv4_host_order >> 24) & 0xFFu),
           (unsigned)((status.peer_ipv4_host_order >> 16) & 0xFFu),
           (unsigned)((status.peer_ipv4_host_order >> 8) & 0xFFu),
           (unsigned)(status.peer_ipv4_host_order & 0xFFu),
           status.stream_enabled ? 1u : 0u,
           status.legacy_compat_enabled ? "V3_PAIRED_PLUS_V4" : "V4_ONLY",
           status.scan_session_accepting ? 1u : 0u,
           status.scan_session_epoch, status.ring_depth,
           NOCTI_V4_NETWORK_FRAME_RING_CAPACITY, status.task_core,
           status.task_priority, status.task_stack_watermark_bytes,
           nocti_v4_err_name(status.last_error));
}

void nocti_v4_network_print_health(const nocti_v4_network_t *network)
{
    if (network == NULL || network->runtime == NULL) {
        printf("NETWORK_HEALTH initialized=0\n");
        return;
    }
    network_runtime_t *r = (network_runtime_t *)network->runtime;
    update_heap_health(r);
    printf("NETWORK_HEALTH wifi_init_ok=%" PRIu32 " connect=%" PRIu32
           " disconnect=%" PRIu32 " reconnect=%" PRIu32
           " socket_create_errors=%" PRIu32 " frames_enqueued=%" PRIu32
           " frames_sent=%" PRIu32 " send_errors=%" PRIu32
           " frames_dropped=%" PRIu32 " bytes_sent=%" PRIu32
           " ring_depth=%" PRIu32 " ring_depth_max=%" PRIu32
           " ring_overflow=%" PRIu32 " task_core=%" PRId32
           " task_priority=%" PRIu32 " task_stack_watermark=%" PRIu32
           " free_heap_before_wifi=%" PRIu32 " free_heap_after_wifi=%" PRIu32
           " minimum_free_heap=%" PRIu32 " largest_free_block=%" PRIu32 "\n",
           r->health->wifi_init_ok, r->health->wifi_connect_count,
           r->health->wifi_disconnect_count, r->health->wifi_reconnect_count,
           r->health->udp_socket_create_errors, r->health->udp_frames_enqueued,
           r->health->udp_frames_sent, r->health->udp_send_errors,
           r->health->udp_frames_dropped, r->health->udp_bytes_sent,
           r->health->network_frame_ring_depth,
           r->health->network_frame_ring_depth_max,
           r->health->network_frame_ring_overflow,
           (int32_t)r->health->network_task_core,
           r->health->network_task_priority,
           r->health->network_task_stack_watermark_bytes,
           r->health->free_heap_before_wifi, r->health->free_heap_after_wifi,
           r->health->minimum_free_heap, r->health->largest_free_block);
}
