"""Batch 1 production C probes; host logic only, no ESP-IDF or hardware."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPONENTS = ROOT / "firmware/v4-idf61/components"


def function(source, name):
    start = source.index("static ", source.rfind("\n", 0, source.index(name + "(")))
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class Batch1C(unittest.TestCase):
    def setUp(self):
        self.cc = os.environ.get("NN2_BATCH1_CC") or shutil.which("cc") or shutil.which("cl")
        self.assertTrue(self.cc, "Native C compiler required; missing compiler is not PASS")
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.work = Path(tmp.name)

    def compile(self, source, success=True):
        path = self.work / "probe.c"
        path.write_text(source)
        exe = self.work / ("probe.exe" if os.name == "nt" else "probe")
        includes = [COMPONENTS / "nocti_v4_common/include",
                    COMPONENTS / "nocti_v4_health/include",
                    COMPONENTS / "nocti_v4_scheduler/include",
                    ROOT / "test/host", ROOT / "test/host/unity"]
        if Path(self.cc).name.lower() in ("cl", "cl.exe"):
            args = [self.cc, "/nologo", "/std:c11", "/W3", str(path), "/Fe:" + str(exe)]
            args += ["/I" + str(p) for p in includes]
        else:
            args = [self.cc, "-std=c11", "-Wall", "-Wextra", str(path), "-o", str(exe)]
            args += ["-I" + str(p) for p in includes]
        result = subprocess.run(args, cwd=self.work, capture_output=True, text=True)
        if not success:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("equal target capacities", result.stdout + result.stderr)
            return
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_sample_clock_unity_suite_including_late_sample_cases(self):
        result = self.compile("""
#define UNITY_SUPPORT_64
#include "unity.c"
#include "test_v4_sample_clock.c"
void setUp(void) {}
void tearDown(void) {}
int main(void) {
    UNITY_BEGIN();
    test_v4_sample_clock_suite();
    return UNITY_END();
}
""")
        self.assertIn("14 Tests 0 Failures 0 Ignored", result)

    def test_equal_tail_guard_and_original_ten_sample_means(self):
        source = (COMPONENTS / "nocti_v4_optical/nocti_v4_optical.c").read_text()
        guard = re.search(r'_Static_assert\(NOCTI_V4_K_ON_TARGET.*?;', source, re.S).group()
        prefix = '#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n#include "nocti_v4_scheduler_config.h"\n'
        source = prefix + guard + "\n" + """
#define NOCTI_V4_ADS_CHANNEL_COUNT 4u
typedef struct { int32_t channel[4]; } nocti_v4_raw_sample_t;
""" + function(source, "tail_add") + "\n" + function(source, "latest_contiguous_mean")
        source += """
int main(void) {
    int32_t on[10][4] = {{0}}, off[10][4] = {{0}}, mean[4];
    uint8_t count[2] = {0}, next[2] = {0};
    for (unsigned phase = 0; phase < 2; ++phase) {
        int32_t (*tail)[4] = phase ? off : on;
        assert(latest_contiguous_mean((const int32_t (*)[4])tail, 0, 0, mean) == 0);
        for (int n = 1; n <= 37; ++n) {
            nocti_v4_raw_sample_t sample = {{n, -n, INT32_MAX, INT32_MIN}};
            tail_add(tail, &count[phase], &next[phase], &sample);
            unsigned used = (unsigned)n < 10u ? (unsigned)n : 10u;
            assert(latest_contiguous_mean((const int32_t (*)[4])tail,
                count[phase], next[phase], mean) == used);
            int64_t sum = 0;
            for (int i = n - (int)used + 1; i <= n; ++i) sum += i;
            assert(mean[0] == sum / used && mean[1] == -sum / used);
            assert(mean[2] == INT32_MAX && mean[3] == INT32_MIN);
        }
    }
    return 0;
}
"""
        self.compile(source)
        mismatch = prefix + '#undef NOCTI_V4_K_OFF_TARGET\n#define NOCTI_V4_K_OFF_TARGET 9u\n' + guard
        self.compile(mismatch, success=False)

    def test_production_commit_histogram_zero_one_two_three_plus_early_and_stale(self):
        source = (COMPONENTS / "nocti_v4_scheduler/nocti_v4_scheduler.c").read_text()
        commit = function(source, "note_sample_commit")
        record_fields = sorted(set(re.findall(r'record->(\w+)', commit)))
        code = """
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "nocti_v4_sample_clock.h"
#include "nocti_v4_health.h"
#define NOCTI_V4_SLOT_COUNT 20u
"""
        code += "typedef struct { " + "; ".join("uint32_t " + f for f in record_fields) + "; } slot_record_t;\n"
        code += """
typedef struct {
 uint32_t sample_sequence, sample_generation, sample_valid;
} nocti_v4_async_led_completion_t;
typedef struct {
 nocti_v4_sample_geometry_t sample_geometry;
 uint32_t sample_origin, sample_origin_valid, sample_generation;
 uint32_t sync_on_planned[20], sync_on_actual[20], sync_on_known[20];
 uint32_t sync_off_planned[20], sync_off_actual[20], sync_off_known[20];
 nocti_v4_health_t *health;
} scheduler_runtime_t;
/* Single-thread unit adapters; concurrency is not asserted by this probe. */
static void counter_add(uint32_t *p, uint32_t n) { *p += n; }
static void update_max(uint32_t *p, uint32_t n) { if (n > *p) *p = n; }
""" + commit + """
int main(void) {
 nocti_v4_health_t health = {0};
 scheduler_runtime_t r = {0};
 slot_record_t rec = {0};
 nocti_v4_async_led_completion_t c = {0};
 r.health = &health; r.sample_origin = UINT32_MAX - 5u;
 r.sample_origin_valid = 1; r.sample_generation = 7;
 assert(nocti_v4_sample_geometry_for(10, 2200, &r.sample_geometry));
 rec.sample_plan_valid = 1; rec.frame_sequence = 1;
 c.sample_valid = 1; c.sample_generation = 7;
 const uint32_t late[] = {0, 1, 2, 3, 9};
 for (int phase = 0; phase < 2; ++phase) {
   uint32_t edge = r.sample_origin + (phase ? 0u : r.sample_geometry.off_phase_samples);
   for (unsigned i = 0; i < 5; ++i) {
     c.sample_sequence = edge - 1u + late[i];
     note_sample_commit(&r, &rec, phase, &c);
   }
   c.sample_sequence = edge - 2u;
   note_sample_commit(&r, &rec, phase, &c);
 }
 assert(health.sample_clock_late_1 == 2 && health.sample_clock_late_2 == 2);
 assert(health.sample_clock_late_3p == 4 && health.sample_clock_late_max == 9);
 assert(health.sample_clock_late_commits == 8 && health.sample_clock_early_commits == 2);
 c.sample_sequence += 10; c.sample_generation = 6;
 note_sample_commit(&r, &rec, 1, &c);
 c.sample_generation = 7; c.sample_valid = 0;
 note_sample_commit(&r, &rec, 0, &c);
 assert(health.sample_clock_late_commits == 8 && health.sample_clock_late_max == 9);
 return 0;
}
"""
        self.compile(code)

    def test_boot_battery_cache_and_i2c_handoff(self):
        app = (ROOT / "firmware/v4-idf61/main/app_main.c").read_text()
        control = (ROOT / "firmware/v4-idf61/main/v4_nn2_control.inc").read_text()
        cache = app[app.index("#define BATTERY_IDLE_INTERVAL_US"):
                    app.index('#include "v4_nn2_control.inc"')]
        boot = function(app, "boot_power")
        formatter = function(control, "control_read_battery")
        start = function(control, "control_start")
        polling = control[control.index("static i2c_master_dev_handle_t s_battery_gauge;"):
                          control.index("static void control_worker(")]
        worker = function(control, "control_worker")
        stop_schedule = re.search(r"if \(result==ESP_OK\) s_battery_due_us[^;]+;", worker)[0]
        self.assertLess(worker.index("scan_stop(&stop_report)"), worker.index(stop_schedule))
        self.assertLess(worker.index(stop_schedule), worker.index("control_poll_battery();"))
        self.assertLess(worker.index("result=control_release_gauge()"),
                        worker.index("nocti_v4_led_reinit("))
        main = app[app.index("void app_main(void)"):]
        self.assertLess(main.index("nocti_v4_board_early_safe_init("), main.index("boot_power()"))
        self.assertIn("power_release == ESP_OK ?\n            nocti_v4_i2c_init_sync", main)
        self.compile(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_TIMEOUT 263
typedef struct { uint32_t voltage_uv, soc_millipercent; } nn2_board_power_sample_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned lock_depth;
static void portENTER_CRITICAL(portMUX_TYPE *lock)
{ assert(*lock == 0); ++*lock; ++lock_depth; }
static void portEXIT_CRITICAL(portMUX_TYPE *lock)
{ assert(*lock == 1); --*lock; --lock_depth; }
static int64_t time_us = 1000000;
static int64_t esp_timer_get_time(void) { assert(!lock_depth); return time_us; }
static esp_err_t init_error, read_error, release_error;
static unsigned inits, reads, releases, disables;
static esp_err_t nn2_board_power_init_after_safe_gpio(bool enabled)
{ assert(enabled); ++inits; return init_error; }
static esp_err_t nn2_board_power_read(nn2_board_power_sample_t *sample)
{
    ++reads;
    if (!read_error) *sample = (nn2_board_power_sample_t){4000000, 50500};
    return read_error;
}
static esp_err_t nn2_board_power_release_i2c(void) { ++releases; return release_error; }
static esp_err_t nn2_board_power_set_charge_enabled(bool enabled)
{ assert(!enabled); ++disables; return ESP_OK; }
static void report_result(const char *stage, esp_err_t err) { (void)stage; (void)err; }
/* START and idle-window fakes drive the actual admission and poll code. */
enum {NN2_OPTICAL_IDLE, NN2_OPTICAL_STARTING, NN2_OPTICAL_RUNNING};
enum {NN2_OK, NN2_ERR_INVALID_ARG, NN2_ERR_INVALID_STATE};
typedef int nn2_status_t;
typedef struct { unsigned fs, current_ma[20]; } nn2_port_optical_config_t;
#define NN2_OPTICAL_MAX_MA 30
static portMUX_TYPE s_control_lock;
static unsigned s_control_generation;
static bool s_control_clean=true, s_control_working, s_control_wanted, s_control_stop, s_control_fault;
static uint8_t s_control_state;
static nn2_port_optical_config_t s_control_config;
static int s_control_diag, worker_task, *s_control_worker=&worker_task;
static void nocti_v4_stop_diag_reset(int *diag) { *diag = 0; }
static void xTaskNotifyGive(int *task) { assert(task == &worker_task); }

#define NOCTI_V4_I2C_SYNC_DIAGNOSTIC_HZ 100000
#define NOCTI_V4_I2C_TRANS_QUEUE_DEPTH 0
#define PCA_IO_MODE_SYNC 0
static int s_async_led;
static int nocti_v4_async_led_mode(const int *led) { return *led; }
typedef int *i2c_master_dev_handle_t;
static int bus, pca, gauge;
static struct {
    bool initialized;
    int *bus, *pca;
    unsigned clock_hz, queue_depth;
} s_i2c = {true, &bus, &pca, 100000, 0};

""" + cache + boot + formatter + start + r"""
enum {NO_START, START_ON_ATTACH, START_DURING_READ};
static int inject_start;
static unsigned attaches, gauge_reads, removals;
static esp_err_t gauge_result, remove_result;
static bool attached, transferring;
static void request_start(void)
{
    const nn2_port_optical_config_t config = {.fs = 5};
    assert(control_start(NULL, &config) == NN2_OK);
    assert(s_control_state == NN2_OPTICAL_STARTING && s_control_wanted);
}
static esp_err_t nn2_board_power_add_gauge(int *handle, i2c_master_dev_handle_t *out)
{
    assert(handle == &bus && !lock_depth && !attached);
    ++attaches;
    attached = true;
    *out = &gauge;
    if (inject_start == START_ON_ATTACH) request_start();
    return ESP_OK;
}
static esp_err_t nn2_board_power_read_gauge(i2c_master_dev_handle_t handle,
                                           nn2_board_power_sample_t *sample)
{
    assert(handle == &gauge && attached && !lock_depth && !s_control_wanted);
    ++gauge_reads;
    transferring = true;
    if (inject_start == START_DURING_READ) request_start();
    time_us += 18000;
    if (gauge_result == ESP_OK) *sample = (nn2_board_power_sample_t){3900000, 47000};
    transferring = false;
    return gauge_result;
}
static esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle)
{
    assert(handle == &gauge && attached && !transferring && !lock_depth);
    ++removals;
    if (remove_result == ESP_OK) attached = false;
    return remove_result;
}
""" + polling + "\nstatic void schedule_after_stop(esp_err_t result) {" + stop_schedule + "}" + r"""
static void stopped(void)
{
    s_control_state = NN2_OPTICAL_IDLE;
    s_control_wanted = false;
    s_control_clean = true;
    inject_start = NO_START;
    schedule_after_stop(ESP_OK);
}

static void expect_reply(const char *expected)
{
    uint8_t text[128];
    assert(control_read_battery(text, sizeof(text)) == strlen(expected));
    assert(strcmp((char *)text, expected) == 0);
}
int main(void)
{
    expect_reply("battery_valid=0 battery_uv=0 battery_mperm=0 battery_age_ms=-1 battery_error=259");
    assert(inits == 0 && reads == 0 && releases == 0);
    assert(boot_power() == ESP_OK);
    time_us += 60000123;
    expect_reply("battery_valid=1 battery_uv=4000000 battery_mperm=50500 battery_age_ms=60000 battery_error=0");
    expect_reply("battery_valid=1 battery_uv=4000000 battery_mperm=50500 battery_age_ms=60000 battery_error=0");
    assert(inits == 1 && reads == 1 && releases == 1 && disables == 0);

    s_battery = (v4_battery_cache_t){.error = ESP_ERR_INVALID_STATE};
    read_error = ESP_ERR_TIMEOUT;
    assert(boot_power() == ESP_OK);
    expect_reply("battery_valid=0 battery_uv=0 battery_mperm=0 battery_age_ms=-1 battery_error=263");
    assert(inits == 2 && reads == 2 && releases == 2 && disables == 0);

    init_error = ESP_ERR_TIMEOUT;
    assert(boot_power() == ESP_OK);
    assert(inits == 3 && reads == 2 && releases == 3 && disables == 0);

    init_error = read_error = ESP_OK;
    release_error = ESP_ERR_INVALID_STATE;
    assert(boot_power() == ESP_ERR_INVALID_STATE);
    expect_reply("battery_valid=1 battery_uv=4000000 battery_mperm=50500 battery_age_ms=0 battery_error=259");
    assert(inits == 4 && reads == 3 && releases == 4 && disables == 1);

    time_us = s_battery_due_us - 1;
    control_poll_battery();
    assert(attaches == 0);
    ++time_us;
    control_poll_battery();
    assert(attaches == 1 && gauge_reads == 1 && removals == 1);
    expect_reply("battery_valid=1 battery_uv=3900000 battery_mperm=47000 battery_age_ms=0 battery_error=0");
    assert(attaches == 1 && gauge_reads == 1 && removals == 1);

    /* A due attempt is deferred during acquisition and an unproven SYNC window. */
    time_us = s_battery_due_us;
    s_control_state = NN2_OPTICAL_RUNNING;
    control_poll_battery();
    assert(attaches == 1);
    schedule_after_stop(ESP_ERR_TIMEOUT);
    assert(s_battery_due_us == time_us);
    stopped();
    assert(s_battery_due_us == time_us + 5000000);
    time_us += 4999999;
    control_poll_battery();
    assert(attaches == 1);
    ++time_us;
    s_i2c.clock_hz = 800000;
    control_poll_battery();
    s_i2c.clock_hz = 100000;
    s_i2c.queue_depth = 4;
    control_poll_battery();
    s_i2c.queue_depth = 0;
    s_async_led = 1;
    control_poll_battery();
    assert(attaches == 1 && s_battery_due_us == time_us);
    s_async_led = PCA_IO_MODE_SYNC;
    time_us += 10000000;
    control_poll_battery();
    control_poll_battery();
    assert(attaches == 2 && gauge_reads == 2 && removals == 2);

    /* A failed refresh preserves the last successful sample and its age. */
    time_us = s_battery_due_us;
    gauge_result = ESP_ERR_TIMEOUT;
    control_poll_battery();
    expect_reply("battery_valid=1 battery_uv=3900000 battery_mperm=47000 battery_age_ms=30000 battery_error=263");
    assert(attaches == 3 && gauge_reads == 3 && removals == 3);

    stopped();
    request_start();
    time_us = s_battery_due_us;
    control_poll_battery();
    assert(attaches == 3);
    stopped();
    time_us = s_battery_due_us;
    inject_start = START_ON_ATTACH;
    control_poll_battery();
    assert(attaches == 4 && gauge_reads == 3 && removals == 4 && !attached);
    assert(s_battery_due_us == time_us);

    stopped();
    time_us = s_battery_due_us;
    gauge_result = ESP_OK;
    inject_start = START_DURING_READ;
    control_poll_battery();
    assert(attaches == 5 && gauge_reads == 4 && removals == 5 && !attached);
    assert(s_control_state == NN2_OPTICAL_STARTING);
    expect_reply("battery_valid=1 battery_uv=3900000 battery_mperm=47000 battery_age_ms=0 battery_error=0");

    stopped();
    time_us = s_battery_due_us;
    remove_result = ESP_ERR_INVALID_STATE;
    control_poll_battery();
    assert(attached && s_battery_gauge == &gauge);
    expect_reply("battery_valid=1 battery_uv=3900000 battery_mperm=47000 battery_age_ms=0 battery_error=259");
    request_start();
    assert(control_release_gauge() == ESP_ERR_INVALID_STATE && attached);
    remove_result = ESP_OK;
    assert(control_release_gauge() == ESP_OK && !attached && s_battery_gauge == NULL);
    return 0;
}
""")

    def test_actual_report_formatter_fits_ten_minute_bench_and_preserves_keys(self):
        source = (ROOT / "firmware/v4-idf61/main/v4_nn2_control.inc").read_text()
        formatter = function(source, "control_read_diag")
        code = """
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "nocti_v4_health.h"
#include "nocti_v4_stop_diag.h"
#define __ATOMIC_RELAXED 0
#define __atomic_load_n(p, order) (*(p))
#define __atomic_store_n(p, n, order) (*(p) = (n))
#define HLOAD(field) (s_health.field)
static nocti_v4_health_t s_health;
static struct { uint32_t packets_sent, dropped, highwater; } raw_storage, *s_raw=&raw_storage;
static int s_ads, s_async_led, s_scheduler, s_optical;
static int s_control_worker = 1, s_control_network = 1;
static unsigned uxTaskGetStackHighWaterMark(int handle) { (void)handle; return 500; }
static const char *control_boot_stage(void) { return "ready"; }
#define nocti_v4_task_wdt_log_pending() ((void)0)
#define nocti_v4_task_wdt_subscribe_errors() 0u
#define nocti_v4_task_wdt_kick_errors() 0u
#define nocti_v4_ads_sample_stack_watermarks(p) ((void)(p))
#define nocti_v4_async_led_sample_stack_watermark(p) ((void)(p))
#define nocti_v4_scheduler_sample_stack_watermark(p) ((void)(p))
#define nocti_v4_optical_sample_stack_watermarks(p) ((void)(p))
#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_8BIT 2u
#define NN2_OPTICAL_IDLE 0
/* Stubs for the get_netdiag branch (kind 3). Health and timing do not call these. */
typedef struct { uint8_t socket_open; uint32_t received, replied, invalid, socket_errors, send_errors;
  uint32_t frame_send_errors, frame_send_eagain, frame_send_enobufs, frame_send_other;
  int last_errno, frame_last_errno; } nn2_net_status_t;
static nn2_net_status_t nn2_net_get_status(void) { nn2_net_status_t s = {0}; return s; }
static unsigned char control_state(void *ctx) { (void)ctx; return NN2_OPTICAL_IDLE; }
static void control_diag_snapshot(nocti_v4_stop_diag_t *out) { nocti_v4_stop_diag_reset(out); }
static size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 0u; }
static size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return 0u; }
static size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 0u; }
"""
        for name, var in (("nocti_v4_ads_status_snapshot_t", "adc"),
                          ("nocti_v4_scheduler_status_t", "sched")):
            fields = sorted(set(re.findall(var + r"\.(\w+)", formatter)))
            code += "typedef struct { " + "; ".join(
                ("int " if field == "last_error" else "uint32_t ") + field for field in fields) + "; } " + name + ";\n"
        code += """
#define nocti_v4_ads_get_status(p, out) ((void)(p), memset(out, 0, sizeof(*(out))))
#define nocti_v4_scheduler_get_status(p, out) ((void)(p), memset(out, 0, sizeof(*(out))))
"""
        lists = {
            "nocti_v4_scheduler_append_led_timing": " i2c_on_us=" + ",".join(["223"] * 20) + " i2c_off_us=" + ",".join(["147"] * 20),
            "nocti_v4_scheduler_append_sync": " fs=8000 anchor=4800000 frame_delta=800 slot_samples=40 off_samples=18 missed=0 catchup=0 gap=0 edge_miss=0 on_d=" + ",".join(["1"] * 20) + " off_d=" + ",".join(["1"] * 20)}
        for name, text in lists.items():
            code += 'static size_t ' + name + '(int *p, char *out, size_t cap) { (void)p; int n = snprintf(out, cap, "' + text + '"); return n > 0 && (size_t)n < cap ? (size_t)n : 0; }\n'
        code += formatter + """
int main(void) {
 uint8_t out[1440u - 28u + 1u - 64u];
 s_raw->packets_sent = 48000; s_raw->dropped = 4800000; s_raw->highwater = 128;
 s_health.free_heap_before_wifi = 120000;
 s_health.free_heap_after_wifi = 42000;
 s_health.largest_free_block = 24000;
 s_health.adc_ok = 4800000;
 s_health.sample_clock_late_commits = 240000;
 s_health.sample_clock_late_1 = 240000;
 s_health.sample_clock_late_max = 1;
 s_health.udp_frames_sent = 6000;
 s_health.async_callback_core = s_health.async_led_task_core = s_health.scheduler_task_core = 1;
 for (uint8_t kind = 0; kind < 2; ++kind) {
   size_t n = control_read_diag(NULL, kind, out, sizeof(out));
   assert(n > 0 && n < sizeof(out));
   puts((char *)out);
 }
 return 0;
}
"""
        reports = self.compile(code).splitlines()
        self.assertEqual(len(reports), 2)
        for report in reports:
            fields = dict(item.split("=", 1) for item in report.split())
            self.assertLessEqual(len(report), 1412)
            for key in ("sample_clock_early_commits", "late_1", "late_2", "late_3p", "late_max",
                        "async_completion_errors", "async_submit_errors", "async_stale_completion",
                        "async_callback_core", "async_led_task_core", "scheduler_task_core",
                        "udp_frames_sent", "udp_frames_dropped", "raw_packets_sent",
                        "raw_dropped_samples", "raw_buffer_highwater"):
                self.assertIn(key, fields)
            self.assertEqual(fields["late_1"], "240000")
            self.assertEqual(fields["late_max"], "1")
        self.assertIn("free_heap_before_wifi=120000", reports[0])
        self.assertIn("free_heap_after_wifi=42000", reports[0])
        self.assertIn("largest_free_block=24000", reports[0])
        self.assertIn("i2c_on_us=", reports[0])
        self.assertIn("on_d=", reports[1])


if __name__ == "__main__":
    unittest.main()
