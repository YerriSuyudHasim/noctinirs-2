/* Draft v0.3 payload assets only; no runtime binding. Caller-owned, nonblocking. */
#ifndef NN2_CORE_OPTICAL_CODEC_H
#define NN2_CORE_OPTICAL_CODEC_H
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#define NN2_OPT2_PREFIX_BYTES 24u
#define NN2_OPT2_SLOT_BYTES 64u
#define NN2_OPT2_DETECTOR_BYTES 13u
#define NN2_OPT2_PAYLOAD_BYTES 1384u
#define NN2_RAW_PREFIX_BYTES 16u
#define NN2_RAW_SAMPLE_BYTES 14u
#define NN2_RAW_PAYLOAD_MAX 1416u
#define NN2_OPTICAL_SUM_EMPTY INT32_MIN
#define NN2_OPTICAL_SD_EMPTY UINT16_MAX
#define NN2_OPTICAL_SD_CLIPPED 0xFFFEu
#define NN2_OPTICAL_STAB_EMPTY 0xFFu
#define NN2_OPTICAL_NO_WITNESS 0xFFu
typedef enum { NN2_OC_OK=0, NN2_OC_INVALID, NN2_OC_TOO_LARGE,
    NN2_OC_UNKNOWN_LAYOUT, NN2_OC_TRUNCATED } nn2_oc_result_t;
typedef enum { NN2_OC_INVALID_QUALITY=0, NN2_OC_VALID_MINIMUM=1,
    NN2_OC_VALID_TARGET=2, NN2_OC_ABORTED_BY_STOP=3,
    NN2_OC_STARTUP_WARMUP=4 } nn2_oc_quality_t;
typedef struct { uint32_t oversized_payloads, unsupported_sources,
    unsupported_layouts, warning_slots; } nn2_oc_stats_t;
typedef struct { int32_t sum_on, sum_off; uint16_t sd_on_q4, sd_off_q4;
    uint8_t det_flags; } nn2_oc_detector_t;
typedef struct {
    uint8_t slot_index, quality; uint16_t reason;
    uint8_t n_on_used, n_off_used, n_on_available, n_off_available;
    uint8_t on_commit_late, off_commit_late, witness_ain, verified_mask;
    nn2_oc_detector_t det[4];
} nn2_oc_slot_t;
typedef struct {
    uint16_t slot_plan_epoch; uint32_t frame_sequence, first_sample_sequence;
    uint16_t sample_count, frame_flags, geometry_map_id, gate_config_id;
    nn2_oc_slot_t slot[20]; uint8_t stab_q[80];
} nn2_oc_opt2_t;
typedef struct { int32_t ain[4]; uint8_t phase_class, slot_index; } nn2_oc_raw_sample_t;
typedef struct {
    uint16_t slot_plan_epoch; uint32_t first_sample_sequence;
    uint16_t sample_count, raw_flags, geometry_map_id;
    nn2_oc_raw_sample_t sample[100];
} nn2_oc_raw_t;
/* Stats is required: counts attempted oversized payloads, saturating.
 * Failures leave output and written unchanged. No envelope, CRC or fragmentation.
 * Separate typed encoders set adc_source/layout; decoder dispatch rejects others.
 * Structs are values, never wire overlays. No global state or dynamic allocation. */
nn2_oc_result_t nn2_oc_encode_opt2(const nn2_oc_opt2_t *, uint8_t *, size_t,
    size_t *written, nn2_oc_stats_t *);
nn2_oc_result_t nn2_oc_encode_raw(const nn2_oc_raw_t *, uint8_t *, size_t,
    size_t *written, nn2_oc_stats_t *);
nn2_oc_result_t nn2_oc_decode(const uint8_t *, size_t, nn2_oc_opt2_t *,
    nn2_oc_raw_t *, nn2_oc_stats_t *);
#endif
