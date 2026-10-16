#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "nocti_v3_stream.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "nocti_v3_config.h"
#include "nocti_v3_version.h"

static void normalize_command(const uint8_t *input, size_t input_length,
                              char output[NOCTI_V3_COMMAND_MAX_BYTES])
{
    size_t count = input_length;
    if (count >= NOCTI_V3_COMMAND_MAX_BYTES) count = NOCTI_V3_COMMAND_MAX_BYTES - 1u;
    for (size_t i = 0u; i < count; ++i)
        output[i] = (char)toupper((unsigned char)input[i]);
    output[count] = '\0';
    while (count > 0u && isspace((unsigned char)output[count - 1u]))
        output[--count] = '\0';
    size_t start = 0u;
    while (output[start] != '\0' && isspace((unsigned char)output[start])) ++start;
    if (start > 0u) memmove(output, &output[start], strlen(&output[start]) + 1u);
}

static bool command_is(const char *command, const char *name)
{
    const size_t n = strlen(name);
    return strncmp(command, name, n) == 0 &&
           (command[n] == '\0' || isspace((unsigned char)command[n]));
}

static bool parse_u32_argument(const char *command, const char *prefix, uint32_t *value)
{
    if (command == NULL || prefix == NULL || value == NULL) return false;
    const size_t n = strlen(prefix);
    if (strncmp(command, prefix, n) != 0) return false;
    char extra = '\0';
    unsigned long parsed = 0u;
    if (sscanf(command + n, "%lu %c", &parsed, &extra) != 1 || parsed > UINT32_MAX)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

static esp_err_t write_reply(char *reply, size_t capacity, size_t *out_length,
                             const char *format, ...)
    __attribute__((format(printf, 4, 5)));
static esp_err_t write_reply(char *reply, size_t capacity, size_t *out_length,
                             const char *format, ...)
{
    if (reply == NULL || capacity == 0u || out_length == NULL || format == NULL)
        return ESP_ERR_INVALID_ARG;
    va_list args; va_start(args, format);
    const int n = vsnprintf(reply, capacity, format, args);
    va_end(args);
    if (n < 0) return ESP_FAIL;
    if ((size_t)n >= capacity) {
        reply[capacity - 1u] = '\0'; *out_length = capacity - 1u;
        return ESP_ERR_INVALID_SIZE;
    }
    *out_length = (size_t)n;
    return ESP_OK;
}

esp_err_t nocti_v3_stream_init(nocti_v3_stream_t *stream,
                               nocti_v3_board_t *board,
                               nocti_v3_timebase_t *timebase,
                               nocti_v3_adc_t *adc,
                               nocti_v3_led_t *led,
                               nocti_v3_optical_t *optical,
                               nocti_v3_health_t *health)
{
    if (stream == NULL || board == NULL || timebase == NULL || adc == NULL ||
        led == NULL || optical == NULL || health == NULL) return ESP_ERR_INVALID_ARG;
    memset(stream, 0, sizeof(*stream));
    stream->board = board; stream->timebase = timebase; stream->adc = adc;
    stream->led = led; stream->optical = optical; stream->health = health;
    stream->preferred_mode = NOCTI_V3_MODE_DARK;
    return ESP_OK;
}

static esp_err_t reply_log_status(char *reply, size_t cap, size_t *out)
{
    return write_reply(reply, cap, out,
        "NOCTI LOGSTATUS default=%d maximum=%d master=%d wildcard=%d "
        "main=%d wifi=%d wifi_driver=%d netif=%d dynamic=1 "
        "console=USB_SERIAL_JTAG gpio_dm=19 gpio_dp=20 secondary=NONE runtime_quiet=DISABLED\n",
        (int)CONFIG_LOG_DEFAULT_LEVEL, (int)CONFIG_LOG_MAXIMUM_LEVEL,
        (int)esp_log_get_level_master(), (int)esp_log_get_default_level(),
        (int)esp_log_level_get("nocti_v3_main"),
        (int)esp_log_level_get("nocti_v3_wifi"),
        (int)esp_log_level_get("wifi"),
        (int)esp_log_level_get("esp_netif_lwip"));
}

static esp_err_t reply_status(nocti_v3_stream_t *s, char *reply,
                              size_t cap, size_t *out)
{
    nocti_v3_health_snapshot_t sys; nocti_v3_adc_health_snapshot_t adc;
    nocti_v3_led_health_snapshot_t led; nocti_v3_optical_health_snapshot_t opt;
    nocti_v3_health_snapshot(s->health, &sys);
    nocti_v3_adc_health_snapshot(s->adc, &adc);
    nocti_v3_led_health_snapshot(s->led, &led);
    nocti_v3_optical_health_snapshot(s->optical, &opt);
    uint64_t now = 0u; const bool time_ok =
        nocti_v3_timebase_now_us(s->timebase, &now) == ESP_OK;
    const bool sensor_fault = sys.sensor_fault ||
        sys.lifecycle == NOCTI_V3_STATE_FAULTED ||
        adc.session_integrity_fault || opt.last_error != ESP_OK;
    return write_reply(reply, cap, out,
        "FNIRS STATUS firmware=%s version=%s phase=%s acquisition_running=%u mode=%s state=%s "
        "adc_ready=%u rate_valid=%u measured_sps=%lu session=%lu integrity_fault=%u "
        "scheduler=READY processing=READY frame_ring=READY scan_armed=%u "
        "config_gen=%lu frame=%lu slot=%u slot_state=%s frame_qdepth=%lu "
        "led_ready=%u led_state=%s pca_reset=%s i2c_ready=%u i2c_qdepth=%lu "
        "wifi_link=%u stations=%lu network_ready=%u udp_peer_active=%u peer_ip4=0x%08lX "
        "stream_enabled=%u last_command_us=%llu last_tx_us=%llu sensor_fault=%u "
        "ctrl_port=%u stream_port=%u uptime_us=%llu time_valid=%u "
        "cores=adc:%d/sched:%d/proc:%d/frame_dummy:%d/frame_consumer:%d/led:%d/udp_ctrl:%d/udp_stream:%d/health:%d/i2c_init:%d/i2c_cb:%d "
        "fault=%s reason=%s err=%ld\n",
        NOCTI_V3_FW_NAME, NOCTI_V3_FW_VERSION_STRING, NOCTI_V3_BUILD_PHASE,
        adc.acquisition_running ? 1u : 0u, nocti_v3_mode_name(sys.mode),
        nocti_v3_state_name(sys.lifecycle), adc.ready ? 1u : 0u,
        adc.rate_valid ? 1u : 0u, (unsigned long)adc.measured_sps,
        (unsigned long)adc.session_epoch, adc.session_integrity_fault ? 1u : 0u,
        opt.scan_active ? 1u : 0u, (unsigned long)opt.config_generation,
        (unsigned long)opt.frame_index, (unsigned)opt.slot_index,
        nocti_v3_slot_state_name(opt.slot_state),
        (unsigned long)opt.completed_frame_ring_depth,
        led.runtime_ready ? 1u : 0u, nocti_v3_led_state_name(led.state),
        s->board->pca_reset_asserted ? "ASSERTED_LOW" : "RELEASED",
        s->board->i2c_ready ? 1u : 0u, (unsigned long)s->board->i2c_queue_depth,
        sys.wifi_ap_started ? 1u : 0u, (unsigned long)sys.wifi_station_count,
        sys.network_ready ? 1u : 0u, sys.udp_peer_active ? 1u : 0u,
        (unsigned long)sys.udp_peer_ip4, sys.stream_enabled ? 1u : 0u,
        (unsigned long long)sys.last_command_receive_us,
        (unsigned long long)sys.last_successful_transmit_us,
        sensor_fault ? 1u : 0u,
        (unsigned)NOCTI_V3_UDP_CTRL_PORT, (unsigned)NOCTI_V3_UDP_STREAM_PORT,
        (unsigned long long)now, time_ok ? 1u : 0u, adc.adc_task_core,
        opt.scheduler_task_core, opt.processing_task_core,
        opt.frame_sink_task_core, opt.frame_consumer_task_core,
        led.led_task_core, sys.udp_task_core, sys.stream_task_core,
        sys.health_task_core, led.i2c_init_core, led.i2c_callback_core,
        nocti_v3_fault_name(sys.last_fault),
        nocti_v3_optical_reason_name(opt.last_reason),
        (long)(opt.last_error != ESP_OK ? opt.last_error :
               led.last_error != ESP_OK ? led.last_error : adc.last_error));
}

static esp_err_t reply_health(nocti_v3_stream_t *s, char *reply,
                              size_t cap, size_t *out)
{
    nocti_v3_adc_health_snapshot_t a; nocti_v3_led_health_snapshot_t l;
    nocti_v3_optical_health_snapshot_t o; nocti_v3_health_snapshot_t sys;
    nocti_v3_adc_health_snapshot(s->adc, &a);
    nocti_v3_led_health_snapshot(s->led, &l);
    nocti_v3_optical_health_snapshot(s->optical, &o);
    nocti_v3_health_snapshot(s->health, &sys);
    return write_reply(reply, cap, out,
        "FNIRS HEALTH phase=%s frames_total=%llu frames_valid=%llu partial_frames=%llu "
        "invalid_frames=%llu slots_total=%llu underk_on=%llu underk_off=%llu "
        "subtarget_on=%llu subtarget_off=%llu min_n_on=%lu min_n_off=%lu "
        "adc_ok=%llu adc_missed=%llu adc_sequence_gap=%llu drdy_event_overflow=%llu "
        "adc_ring_overflow=%llu adc_period_min_us=%lu adc_period_max_us=%lu "
        "adc_period_gap=%llu adc_max_gap_us=%lu crc_bad=%llu status_errors=%llu "
        "spi_errors=%llu spi_duration_max_us=%lu consecutive_spi=%lu consecutive_crc=%lu "
        "consecutive_status=%lu consecutive_period=%lu led_errors=%llu i2c_nack=%llu "
        "i2c_timeout=%llu i2c_queue_full=%llu i2c_completion_ring_overflow=%llu "
        "stale_i2c_completion=%llu led_commit_latency_max_us=%lu recovery_count=%llu "
        "consecutive_led=%lu sched_missed=%llu sched_abort=%llu scheduler_lateness_max_us=%lu "
        "scheduler_event_overflow=%llu led_on_stable_min_us=%lu led_off_stable_min_us=%lu "
        "raw_on=%llu raw_I_LED_OFF=%llu reject_transition=%llu reject_settle=%llu "
        "reject_guard=%llu reject_stale=%llu reject_ambiguous=%llu timing_ring_overflow=%llu "
        "sample_scratch_overflow=%llu completed_frame_ring_overflow=%llu frame_ring_depth=%lu "
        "frame_ring_depth_max=%lu frames_consumed=%llu network_frames_sent=%llu network_records_sent=%llu "
        "network_drop=%llu stream_errors=%llu udp_rx=%lu udp_tx=%lu udp_socket_errors=%lu "
        "network_ready=%u peer_active=%u stream_enabled=%u last_command_us=%llu last_tx_us=%llu sensor_fault=%u "
        "cores=adc:%d/dark:%d/sched:%d/proc:%d/frame_dummy:%d/frame_consumer:%d/led:%d/recovery:%d/udp_ctrl:%d/udp_stream:%d/health:%d/i2c_init:%d/i2c_cb:%d "
        "stacks=adc:%lu/dark:%lu/sched:%lu/proc:%lu/frame:%lu/led:%lu/recovery:%lu/udp_ctrl:%lu/udp_stream:%lu/health:%lu "
        "session=%lu config_gen=%lu fault=%s reason=%s esp_err=%ld\n",
        NOCTI_V3_BUILD_PHASE,
        (unsigned long long)o.frames_total, (unsigned long long)o.frames_valid,
        (unsigned long long)o.partial_frames, (unsigned long long)o.invalid_frames,
        (unsigned long long)o.slots_total, (unsigned long long)o.underk_on,
        (unsigned long long)o.underk_off, (unsigned long long)o.subtarget_on,
        (unsigned long long)o.subtarget_off, (unsigned long)o.min_n_on,
        (unsigned long)o.min_n_off, (unsigned long long)a.adc_ok,
        (unsigned long long)a.adc_missed, (unsigned long long)a.adc_sequence_gap,
        (unsigned long long)a.drdy_event_overflow,
        (unsigned long long)a.adc_ring_overflow,
        (unsigned long)a.adc_period_min_us, (unsigned long)a.adc_period_max_us,
        (unsigned long long)a.adc_period_gap, (unsigned long)a.adc_max_gap_us,
        (unsigned long long)a.crc_bad, (unsigned long long)a.ads_status_errors,
        (unsigned long long)a.spi_errors, (unsigned long)a.spi_duration_max_us,
        (unsigned long)a.consecutive_spi_errors,
        (unsigned long)a.consecutive_crc_errors,
        (unsigned long)a.consecutive_status_errors,
        (unsigned long)a.consecutive_period_gaps,
        (unsigned long long)l.led_errors, (unsigned long long)l.i2c_nack,
        (unsigned long long)l.i2c_timeout, (unsigned long long)l.i2c_queue_full,
        (unsigned long long)l.i2c_completion_ring_overflow,
        (unsigned long long)o.stale_i2c_completion,
        (unsigned long)l.led_commit_latency_max_us,
        (unsigned long long)l.recovery_count,
        (unsigned long)l.consecutive_errors,
        (unsigned long long)o.sched_missed, (unsigned long long)o.sched_abort,
        (unsigned long)o.scheduler_lateness_max_us,
        (unsigned long long)o.scheduler_event_overflow,
        (unsigned long)o.led_on_stable_min_us,
        (unsigned long)o.led_off_stable_min_us,
        (unsigned long long)o.raw_stable_on,
        (unsigned long long)o.raw_stable_i_led_off,
        (unsigned long long)o.raw_sample_rejected_transition,
        (unsigned long long)o.raw_sample_rejected_settle,
        (unsigned long long)o.raw_sample_rejected_guard,
        (unsigned long long)o.raw_sample_rejected_stale_epoch,
        (unsigned long long)o.raw_sample_rejected_ambiguous,
        (unsigned long long)o.slot_timing_ring_overflow,
        (unsigned long long)o.sample_scratch_overflow,
        (unsigned long long)o.completed_frame_ring_overflow,
        (unsigned long)o.completed_frame_ring_depth,
        (unsigned long)o.completed_frame_ring_depth_max,
        (unsigned long long)o.completed_frames_consumed,
        (unsigned long long)o.network_frames_sent,
        (unsigned long long)o.network_records_sent,
        (unsigned long long)o.network_drop, (unsigned long long)o.stream_errors,
        (unsigned long)sys.udp_rx_packets, (unsigned long)sys.udp_tx_packets,
        (unsigned long)sys.udp_socket_errors, sys.network_ready ? 1u : 0u,
        sys.udp_peer_active ? 1u : 0u, sys.stream_enabled ? 1u : 0u,
        (unsigned long long)sys.last_command_receive_us,
        (unsigned long long)sys.last_successful_transmit_us,
        sys.sensor_fault ? 1u : 0u,
        a.adc_task_core, a.diagnostic_task_core, o.scheduler_task_core,
        o.processing_task_core, o.frame_sink_task_core,
        o.frame_consumer_task_core, l.led_task_core,
        l.recovery_task_core, sys.udp_task_core, sys.stream_task_core,
        sys.health_task_core, l.i2c_init_core, l.i2c_callback_core,
        (unsigned long)a.adc_task_stack_watermark_bytes,
        (unsigned long)a.diagnostic_task_stack_watermark_bytes,
        (unsigned long)o.scheduler_stack_watermark_bytes,
        (unsigned long)o.processing_stack_watermark_bytes,
        (unsigned long)o.frame_sink_stack_watermark_bytes,
        (unsigned long)l.led_task_stack_watermark_bytes,
        (unsigned long)l.recovery_task_stack_watermark_bytes,
        (unsigned long)sys.udp_task_stack_watermark_bytes,
        (unsigned long)sys.stream_task_stack_watermark_bytes,
        (unsigned long)sys.health_task_stack_watermark_bytes,
        (unsigned long)a.session_epoch, (unsigned long)o.config_generation,
        nocti_v3_fault_name(sys.last_fault),
        nocti_v3_optical_reason_name(o.last_reason),
        (long)(o.last_error != ESP_OK ? o.last_error :
               l.last_error != ESP_OK ? l.last_error : a.last_error));
}

static esp_err_t reply_timing(nocti_v3_stream_t *s, char *reply,
                              size_t cap, size_t *out)
{
    const nocti_v3_timing_config_t *t = nocti_v3_timing_config();
    nocti_v3_adc_health_snapshot_t a; nocti_v3_led_health_snapshot_t l;
    nocti_v3_optical_health_snapshot_t o;
    nocti_v3_adc_health_snapshot(s->adc, &a);
    nocti_v3_led_health_snapshot(s->led, &l);
    nocti_v3_optical_health_snapshot(s->optical, &o);
    const uint32_t wire_400 = (NOCTI_V3_I2C_ATOMIC_LEDOUT_BITS * 1000000u +
                               NOCTI_V3_I2C_CLOCK_DEFAULT_HZ - 1u) /
                              NOCTI_V3_I2C_CLOCK_DEFAULT_HZ;
    const uint32_t wire_1000 = (NOCTI_V3_I2C_ATOMIC_LEDOUT_BITS * 1000000u +
                                NOCTI_V3_I2C_CLOCK_EXPERIMENT_HZ - 1u) /
                               NOCTI_V3_I2C_CLOCK_EXPERIMENT_HZ;
    return write_reply(reply, cap, out,
        "FNIRS TIMING frame_us=%lu slots=%lu slot_us=%lu led_off_us=%lu on_budget_us=%lu "
        "off_budget_us=%lu settle_on_us=%lu settle_off_us=%lu guard_us=%lu adc_target_sps=%lu "
        "adc_period_us=%lu measured_sps=%lu K_target=%u/%u K_min=%u/%u "
        "i2c_selected_hz=%lu i2c_profile=%s i2c_default_hz=%u i2c_experiment_hz=%u LEDOUT_wire_us=%lu/%lu "
        "commit_allowance_us=%lu commit_latency_max_us=%lu on_stable_min_us=%lu "
        "off_stable_min_us=%lu absolute_deadline_ready=%u deadline_armed=%u scan_armed=%u "
        "target_is_design_target_not_hardware_guarantee=1\n",
        (unsigned long)t->frame_period_us, (unsigned long)t->slot_count,
        (unsigned long)t->slot_period_us, (unsigned long)t->led_off_offset_us,
        (unsigned long)t->on_budget_us, (unsigned long)t->off_budget_us,
        (unsigned long)t->settle_on_us, (unsigned long)t->settle_off_us,
        (unsigned long)t->end_guard_us, (unsigned long)t->adc_sample_rate_hz,
        (unsigned long)t->adc_period_us, (unsigned long)a.measured_sps,
        (unsigned)t->k_target_on, (unsigned)t->k_target_off,
        (unsigned)t->k_min_on, (unsigned)t->k_min_off,
        (unsigned long)t->i2c_clock_hz, NOCTI_V3_I2C_PROFILE_NAME,
        (unsigned)NOCTI_V3_I2C_CLOCK_DEFAULT_HZ,
        (unsigned)NOCTI_V3_I2C_CLOCK_EXPERIMENT_HZ,
        (unsigned long)wire_400, (unsigned long)wire_1000,
        (unsigned long)t->i2c_commit_allowance_us,
        (unsigned long)l.led_commit_latency_max_us,
        (unsigned long)o.led_on_stable_min_us,
        (unsigned long)o.led_off_stable_min_us,
        nocti_v3_timebase_ready(s->timebase) ? 1u : 0u,
        s->timebase->deadline_armed ? 1u : 0u, o.scan_active ? 1u : 0u);
}

static esp_err_t reply_registers(nocti_v3_stream_t *s, char *reply,
                                 size_t cap, size_t *out)
{
    nocti_v3_adc_register_snapshot_t r; nocti_v3_adc_register_snapshot(s->adc, &r);
    return write_reply(reply, cap, out,
        "FNIRS REGS ADS131 ID=0x%04X STATUS=0x%04X MODE=0x%04X CLOCK=0x%04X GAIN=0x%04X CFG=0x%04X PCA MODE1=0x40 MODE2=0x00 OCH=STOP\n",
        r.id, r.status, r.mode, r.clock, r.gain, r.cfg);
}

esp_err_t nocti_v3_stream_handle_command(nocti_v3_stream_t *s,
                                         const uint8_t *input,
                                         size_t input_length,
                                         char *reply,
                                         size_t reply_capacity,
                                         size_t *out_reply_length)
{
    if (s == NULL || input == NULL || input_length == 0u || reply == NULL ||
        out_reply_length == NULL) return ESP_ERR_INVALID_ARG;
    char command[NOCTI_V3_COMMAND_MAX_BYTES]; normalize_command(input, input_length, command);
    bool supported = true; esp_err_t command_result = ESP_OK, reply_result = ESP_OK;
    if (command_is(command, "PING")) {
        reply_result = write_reply(reply, reply_capacity, out_reply_length,
            "PONG firmware=%s version=%s phase=%s\n", NOCTI_V3_FW_NAME,
            NOCTI_V3_FW_VERSION_STRING, NOCTI_V3_BUILD_PHASE);
    } else if (command_is(command, "HELLO")) {
        reply_result = write_reply(reply, reply_capacity, out_reply_length,
            "NOCTI HELLO OK firmware=%s ctrl=%u stream=%u mode_default=DARK\n",
            NOCTI_V3_FW_NAME, (unsigned)NOCTI_V3_UDP_CTRL_PORT,
            (unsigned)NOCTI_V3_UDP_STREAM_PORT);
    } else if (command_is(command, "VERSION") || command_is(command, "INFO")) {
        reply_result = write_reply(reply, reply_capacity, out_reply_length,
            "NOCTI VERSION firmware=%s version=%s phase=%s idf=5.5.1 target=esp32s3 packet_fmt=%u\n",
            NOCTI_V3_FW_NAME, NOCTI_V3_FW_VERSION_STRING, NOCTI_V3_BUILD_PHASE,
            (unsigned)NOCTI_V3_PACKET_FORMAT_VER);
    } else if (command_is(command, "LOGSTATUS")) {
        reply_result = reply_log_status(reply, reply_capacity, out_reply_length);
    } else if (command_is(command, "STATUS")) {
        reply_result = reply_status(s, reply, reply_capacity, out_reply_length);
    } else if (command_is(command, "HEALTH")) {
        reply_result = reply_health(s, reply, reply_capacity, out_reply_length);
    } else if (command_is(command, "TIMING")) {
        reply_result = reply_timing(s, reply, reply_capacity, out_reply_length);
    } else if (command_is(command, "REGS")) {
        reply_result = reply_registers(s, reply, reply_capacity, out_reply_length);
    } else if (command_is(command, "DUMP_SLOT_TIMING") ||
               command_is(command, "TRACE")) {
        command_result = nocti_v3_optical_format_slot_timing(
            s->optical, reply, reply_capacity, out_reply_length);
        reply_result = command_result;
    } else if (command_is(command, "HELP")) {
        reply_result = write_reply(reply, reply_capacity, out_reply_length,
            "NOCTI HELP P5 commands: PING HELLO VERSION INFO LOGSTATUS STATUS HEALTH TIMING REGS "
            "DARK SCAN MODE DARK|SCAN START [DARK|SCAN] STOP RESET_COUNTERS DUMP_SLOT_TIMING TRACE "
            "CURRENT 735 0..50 CURRENT 850 0..50 SLOT 0..19. "
            "SCAN performs timestamp classification, latest-contiguous-K averaging, "
            "I_LED_OFF correction, wavelength pairing, and internal frame assembly. "
            "Binary stream-v1 paired ON/I_LED_OFF frames are sent on UDP 3334; MBLL, SSR, bandpass, and UI remain host-side.\n");
    } else if (command_is(command, "DARK") || command_is(command, "MODE DARK")) {
        s->preferred_mode = NOCTI_V3_MODE_DARK;
        reply_result = write_reply(reply, reply_capacity, out_reply_length,
                                   "MODE_OK mode=DARK applies=next_start active_timing_unchanged=1\n");
    } else if (command_is(command, "SCAN") || command_is(command, "MODE SCAN")) {
        s->preferred_mode = NOCTI_V3_MODE_SCAN;
        reply_result = write_reply(reply, reply_capacity, out_reply_length,
                                   "MODE_OK mode=SCAN applies=next_start active_timing_unchanged=1\n");
    } else if (strncmp(command, "CURRENT 735 ", strlen("CURRENT 735 ")) == 0) {
        uint32_t value = 0u;
        if (!parse_u32_argument(command, "CURRENT 735 ", &value) ||
            value > NOCTI_V3_LED_CURRENT_MAX_MA) {
            command_result = ESP_ERR_INVALID_ARG;
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                "CURRENT_ERR wavelength=735 range=0..%u\n",
                (unsigned)NOCTI_V3_LED_CURRENT_MAX_MA);
        } else {
            uint16_t c735 = 0u, c850 = 0u;
            nocti_v3_optical_get_current_profile(s->optical, &c735, &c850);
            command_result = nocti_v3_optical_set_current_profile(
                s->optical, (uint16_t)value, c850);
            if (command_result == ESP_OK)
                reply_result = write_reply(reply, reply_capacity, out_reply_length,
                    "FNIRS CURRENT 735 %lu mA applies=next_start\n",
                    (unsigned long)value);
            else
                reply_result = write_reply(reply, reply_capacity, out_reply_length,
                    "CURRENT_ERR wavelength=735 esp_err=%ld reason=stop_required_or_led_fault\n",
                    (long)command_result);
        }
    } else if (strncmp(command, "CURRENT 850 ", strlen("CURRENT 850 ")) == 0) {
        uint32_t value = 0u;
        if (!parse_u32_argument(command, "CURRENT 850 ", &value) ||
            value > NOCTI_V3_LED_CURRENT_MAX_MA) {
            command_result = ESP_ERR_INVALID_ARG;
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                "CURRENT_ERR wavelength=850 range=0..%u\n",
                (unsigned)NOCTI_V3_LED_CURRENT_MAX_MA);
        } else {
            uint16_t c735 = 0u, c850 = 0u;
            nocti_v3_optical_get_current_profile(s->optical, &c735, &c850);
            command_result = nocti_v3_optical_set_current_profile(
                s->optical, c735, (uint16_t)value);
            if (command_result == ESP_OK)
                reply_result = write_reply(reply, reply_capacity, out_reply_length,
                    "FNIRS CURRENT 850 %lu mA applies=next_start\n",
                    (unsigned long)value);
            else
                reply_result = write_reply(reply, reply_capacity, out_reply_length,
                    "CURRENT_ERR wavelength=850 esp_err=%ld reason=stop_required_or_led_fault\n",
                    (long)command_result);
        }
    } else if (strncmp(command, "SLOT ", strlen("SLOT ")) == 0) {
        uint32_t value = 0u;
        if (!parse_u32_argument(command, "SLOT ", &value) || value >= NOCTI_V3_SLOT_COUNT) {
            command_result = ESP_ERR_INVALID_ARG;
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                                       "SLOT_ERR range=0..19\n");
        } else {
            const nocti_v3_slot_descriptor_t *d = nocti_v3_optical_slot_descriptors();
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                "FNIRS SLOT %lu source=%u wavelength=%u pca=%u current=%u mA note=SCAN_uses_all_slots\n",
                (unsigned long)value, (unsigned)d[value].source_id,
                (unsigned)d[value].wavelength_nm, (unsigned)d[value].pca_output,
                (unsigned)d[value].current_ma);
        }
    } else if (strcmp(command, "START") == 0 || strcmp(command, "START DARK") == 0 ||
               strcmp(command, "START SCAN") == 0) {
        const nocti_v3_mode_t mode = strcmp(command, "START SCAN") == 0 ?
            NOCTI_V3_MODE_SCAN : strcmp(command, "START DARK") == 0 ?
            NOCTI_V3_MODE_DARK : s->preferred_mode;
        command_result = nocti_v3_optical_request_start(s->optical, mode);
        if (command_result == ESP_OK) {
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                "START_OK mode=%s state=%s session=%lu\n",
                nocti_v3_mode_name(mode), nocti_v3_state_name(s->optical->state),
                (unsigned long)s->optical->session_epoch);
        } else {
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                "START_ERR mode=%s esp_err=%ld adc_ready=%u led_ready=%u\n",
                nocti_v3_mode_name(mode), (long)command_result,
                nocti_v3_adc_ready(s->adc) ? 1u : 0u,
                nocti_v3_led_runtime_ready(s->led) ? 1u : 0u);
        }
    } else if (command_is(command, "STOP")) {
        command_result = nocti_v3_optical_stop(s->optical);
        if (command_result == ESP_OK) {
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                                       "STOP_OK state=STOPPED\n");
        } else {
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                                       "STOP_ERR esp_err=%ld\n", (long)command_result);
        }
    } else if (command_is(command, "RESET_COUNTERS")) {
        nocti_v3_health_snapshot_t sys;
        nocti_v3_health_snapshot(s->health, &sys);
        command_result = (sys.lifecycle == NOCTI_V3_STATE_STOPPED &&
                          !nocti_v3_adc_running(s->adc)) ? ESP_OK :
                         ESP_ERR_INVALID_STATE;
        if (command_result == ESP_OK) command_result = nocti_v3_adc_reset_counters(s->adc);
        if (command_result == ESP_OK) command_result = nocti_v3_led_reset_counters(s->led);
        if (command_result == ESP_OK) command_result = nocti_v3_optical_reset_counters(s->optical);
        if (command_result == ESP_OK) nocti_v3_health_reset_runtime_counters(s->health);
        if (command_result == ESP_OK) {
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                                       "RESET_COUNTERS_OK state=STOPPED\n");
        } else {
            reply_result = write_reply(reply, reply_capacity, out_reply_length,
                "RESET_COUNTERS_ERR esp_err=%ld reason=stop_required\n",
                (long)command_result);
        }
    } else if (command_is(command, "MODE SINGLE") || command_is(command, "START SINGLE")) {
        supported = false; command_result = ESP_ERR_NOT_SUPPORTED;
        reply_result = write_reply(reply, reply_capacity, out_reply_length,
            "ERR DEFERRED command=\"%s\" phase=%s reason=unsupported_mode\n",
            command, NOCTI_V3_BUILD_PHASE);
    } else {
        supported = false; command_result = ESP_ERR_NOT_SUPPORTED;
        reply_result = write_reply(reply, reply_capacity, out_reply_length,
                                   "ERR UNKNOWN command=\"%s\"; send HELP\n", command);
    }
    const bool success = command_result == ESP_OK && reply_result == ESP_OK;
    nocti_v3_health_record_command(s->health, supported, success);
    return reply_result != ESP_OK ? reply_result : command_result;
}

uint16_t nocti_v3_stream_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    if (data == NULL && length != 0u) return 0u;
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0u; bit < 8u; ++bit)
            crc = (crc & 0x8000u) != 0u ?
                (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

size_t nocti_v3_stream_build_record(nocti_v3_stream_encoder_t *encoder,
                                    nocti_v3_record_type_t type,
                                    uint64_t timestamp_us,
                                    const void *payload,
                                    size_t payload_length,
                                    uint8_t *output,
                                    size_t output_capacity)
{
    if (encoder == NULL || output == NULL || type == NOCTI_V3_REC_INVALID ||
        payload_length > NOCTI_V3_STREAM_MAX_PAYLOAD_BYTES ||
        (payload_length != 0u && payload == NULL)) return 0u;
    const size_t total = NOCTI_V3_STREAM_HEADER_BYTES + payload_length +
                         NOCTI_V3_STREAM_CRC_BYTES;
    if (output_capacity < total) return 0u;
    nocti_v3_stream_header_v1_t header = {
        .magic = NOCTI_V3_STREAM_MAGIC,
        .version = NOCTI_V3_STREAM_FORMAT_VERSION,
        .type = (uint8_t)type,
        .flags = (uint8_t)NOCTI_V3_STREAM_FLAG_HAS_CRC16,
        .sequence = encoder->sequence++,
        .relative_timestamp_us = (uint32_t)(timestamp_us - encoder->t0_us),
        .payload_length = (uint16_t)payload_length,
    };
    memcpy(output, &header, sizeof(header));
    if (payload_length != 0u)
        memcpy(output + sizeof(header), payload, payload_length);
    const uint16_t crc = nocti_v3_stream_crc16_ccitt_false(
        output, sizeof(header) + payload_length);
    output[sizeof(header) + payload_length] = (uint8_t)(crc >> 8);
    output[sizeof(header) + payload_length + 1u] = (uint8_t)crc;
    return total;
}
