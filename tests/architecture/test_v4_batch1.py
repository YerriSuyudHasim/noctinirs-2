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
#define NN2_OPTICAL_RUNNING 2
/* Mirrors nn2_net_status_t field names; a separate check reads the real header. */
typedef struct { uint8_t socket_open; uint32_t received, replied, invalid, socket_errors, send_errors;
  uint32_t frame_send_errors, frame_send_eagain, frame_send_enobufs, frame_send_other;
  int last_errno, frame_last_errno; } nn2_net_status_t;
static nn2_net_status_t s_net = {1, 1000, 990, 2, 0, 9, 7, 3, 2, 2, 0, 11};
static nn2_net_status_t nn2_net_get_status(void) { return s_net; }
static uint8_t s_control_state_value = NN2_OPTICAL_IDLE;
static uint8_t control_state(void *ctx) { (void)ctx; return s_control_state_value; }
static size_t heap_caps_get_free_size(uint32_t caps) { (void)caps; return 60000u; }
static size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return 41000u; }
static size_t heap_caps_get_largest_free_block(uint32_t caps) { (void)caps; return 24000u; }
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
 s_control_state_value = NN2_OPTICAL_RUNNING;
 size_t running = control_read_diag(NULL, 0, out, sizeof(out));
 assert(running > 0 && running < sizeof(out));
 puts((char *)out);
 return 0;
}
"""
        reports = self.compile(code).splitlines()
        self.assertEqual(len(reports), 3)
        self.assertIn("heap_free_now=60000 heap_free_min=41000 largest_free_block_now=24000", reports[0])
        self.assertIn("heap_free_now=60000 heap_free_min=41000 largest_free_block_now=na", reports[2])
        self.assertIn("frame_send_errors=7 frame_send_eagain=3 frame_send_enobufs=2 frame_send_other=2 frame_last_errno=11", reports[1])
        self.assertNotIn("frame_send_errors", reports[0])
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
