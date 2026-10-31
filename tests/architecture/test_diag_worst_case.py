"""Worst-case reply lengths against the real diagnostic buffer limits.

Functions are extracted from the firmware source and compiled natively; only data
(counter values, heap getters, optical state) is stubbed. Limits are read from the
source files, not from memory. Host-only: no ESP-IDF, no bench.

get_netdiag must fit (test passes). get_health and get_timing are recorded as
expected failures: their pre-Batch-4a content already exceeds the diagnostic
capacity at 10-digit worst-case values. Buffers are not changed here.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / "firmware/v4-idf61/main/v4_nn2_control.inc"
SCHED = ROOT / "firmware/v4-idf61/components/nocti_v4_scheduler/nocti_v4_scheduler.c"
APP = ROOT / "components/nn2_app/src/nn2_app_comm.c"
PROTO = ROOT / "components/nn2_core/include/nn2_core_protocol.h"
NETH = ROOT / "components/nn2_net/include/nn2_net.h"
HEALTH_INCLUDE = ROOT / "firmware/v4-idf61/components/nocti_v4_health/include"
SCHED_INCLUDE = ROOT / "firmware/v4-idf61/components/nocti_v4_scheduler/include"
COMMON_INCLUDE = ROOT / "firmware/v4-idf61/components/nocti_v4_common/include"
IDLE, RUNNING = 0, 2
BS = chr(92)  # backslash, so generated C contains \\n escapes without quoting trouble


def extract_function(source, name):
    """Full text of the function DEFINITION named `name` (not a call or prototype)."""
    for match in re.finditer(re.escape(name) + r"\(", source):
        brace = source.find("{", match.end())
        semicolon = source.find(";", match.end())
        if brace == -1 or (semicolon != -1 and semicolon < brace):
            continue
        start = source.rfind("\n", 0, match.start()) + 1
        depth, end = 0, brace
        while True:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
            if depth == 0:
                return source[start:end]
    raise AssertionError("definition not found: " + name)


def source_limits():
    """Capacities derived from the sources, never from memory."""
    proto = PROTO.read_text()
    payload_max = int(re.search(r"#define NN2_CORE_PAYLOAD_MAX\s+(\d+)u", proto).group(1))
    app = APP.read_text()
    decl = re.search(r"uint8_t diag\[NN2_CORE_PAYLOAD_MAX - (\d+)u \+ (\d+)u\];", app)
    assert decl, "diag buffer declaration changed"
    diag_size = payload_max - int(decl.group(1)) + int(decl.group(2))
    reserve = re.search(r"diag, sizeof\(diag\) - (\d+)u\);", app)
    assert reserve, "read_diag capacity expression changed"
    prefix = int(re.search(r"\.length = (\d+)\}", app).group(1))
    return {"payload_max": payload_max, "diag_size": diag_size,
            "read_cap": diag_size - int(reserve.group(1)), "reply_prefix": prefix}


def mode_suffix():
    match = re.search(r'" (mode=ENGINEERING[^"]*)"', APP.read_text())
    assert match, "ENGINEERING suffix changed"
    return " " + match.group(1)


def struct_fields(text, name):
    match = re.search(r"typedef struct \{(.*?)\} " + name + ";", text, re.S)
    assert match, name
    return re.findall(r"\b(\w+)\s*(?:,|;)", match.group(1))


class WorstCase(unittest.TestCase):
    def setUp(self):
        self.cc = os.environ.get("NN2_BATCH1_CC") or shutil.which("cc") or shutil.which("gcc")
        self.assertTrue(self.cc, "native C compiler required; missing compiler is not PASS")
        self.work = Path(tempfile.mkdtemp(prefix="diag-worst-"))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def build_probe(self, states):
        inc = INC.read_text()
        sched = SCHED.read_text()
        formatter = extract_function(inc, "control_read_diag")
        adc_fields = sorted(set(re.findall(r"\badc\.(\w+)", formatter)))
        sched_fields = sorted(set(re.findall(r"\bsched\.(\w+)", formatter)))
        net_fields = struct_fields(NETH.read_text(), "nn2_net_status_t")
        limits = source_limits()

        def struct_of(name, fields):
            body = " ".join(("int " if f == "last_error" else "uint32_t ") + f + ";" for f in fields)
            return f"typedef struct {{ {body} }} {name};\n"

        net_mirror = ("typedef struct { uint8_t socket_open; uint32_t received, replied, invalid, "
                      "socket_errors, send_errors; uint32_t frame_send_errors, frame_send_eagain, "
                      "frame_send_enobufs, frame_send_other; int last_errno, frame_last_errno; } "
                      "nn2_net_status_t;\n")
        for field in net_fields:
            self.assertIn(field, net_mirror, "nn2_net_status_t mirror out of date: " + field)
        helpers = [extract_function(sched, name) for name in (
            "append_delta_list", "append_latency_list",
            "nocti_v4_scheduler_append_led_timing", "nocti_v4_scheduler_append_sync")]
        calls = []
        for tag, kind, state in states:
            calls.append(
                f'  s_state = {state};\n'
                f'  n = control_read_diag(NULL, {kind}, (uint8_t *)text, sizeof text);\n'
                f'  printf("{tag} %zu{BS}n", n); puts(text);\n'
                f'  printf("{tag}_CAPPED %zu{BS}n", control_read_diag(NULL, {kind}, '
                f'(uint8_t *)capped, READ_CAP));\n')
        preamble = f"""
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#include "nocti_v4_health.h"
#include "nocti_v4_scheduler_config.h"
#define NOCTI_V4_SAMPLE_ADC_HZ 8000u
#define READ_CAP {limits["read_cap"]}u
#define __ATOMIC_RELAXED 0
#define __atomic_load_n(p, order) (*(p))
#define __atomic_store_n(p, n, order) (*(p) = (n))
#define HLOAD(field) (s_health.field)
#define MALLOC_CAP_INTERNAL 1u
#define MALLOC_CAP_8BIT 2u
#define NN2_OPTICAL_IDLE 0
#define NN2_OPTICAL_RUNNING 2
{net_mirror}
static nocti_v4_health_t s_health;
static struct {{ uint32_t packets_sent, dropped, highwater; }} raw_storage, *s_raw = &raw_storage;
static int s_ads, s_async_led, s_optical;
static int s_control_worker = 1, s_control_network = 1;
static int s_state = NN2_OPTICAL_IDLE;
static unsigned uxTaskGetStackHighWaterMark(int handle) {{ (void)handle; return 0xFFFFFFFFu; }}
static const char *control_boot_stage(void) {{ return "not_started"; }}
static unsigned char control_state(void *ctx) {{ (void)ctx; return (unsigned char)s_state; }}
static size_t heap_caps_get_free_size(uint32_t c) {{ (void)c; return 0xFFFFFFFFu; }}
static size_t heap_caps_get_minimum_free_size(uint32_t c) {{ (void)c; return 0xFFFFFFFFu; }}
static size_t heap_caps_get_largest_free_block(uint32_t c) {{ (void)c; return 0xFFFFFFFFu; }}
static nn2_net_status_t s_net;
static nn2_net_status_t nn2_net_get_status(void) {{ return s_net; }}
#define nocti_v4_task_wdt_log_pending() ((void)0)
#define nocti_v4_task_wdt_subscribe_errors() 0u
#define nocti_v4_task_wdt_kick_errors() 0u
#define nocti_v4_ads_sample_stack_watermarks(p) ((void)(p))
#define nocti_v4_async_led_sample_stack_watermark(p) ((void)(p))
#define nocti_v4_scheduler_sample_stack_watermark(p) ((void)(p))
#define nocti_v4_optical_sample_stack_watermarks(p) ((void)(p))
#define nocti_v4_ads_get_status(p, out) ((void)(p), memset((out), 0xFF, sizeof(*(out))), (out)->last_error = INT_MIN)
#define nocti_v4_scheduler_get_status(p, out) ((void)(p), memset((out), 0xFF, sizeof(*(out))), (out)->last_error = INT_MIN)
{struct_of("nocti_v4_ads_status_snapshot_t", adc_fields)}
{struct_of("nocti_v4_scheduler_status_t", sched_fields)}
typedef struct {{
  uint32_t sync_on_i2c_us[NOCTI_V4_SLOT_COUNT], sync_off_i2c_us[NOCTI_V4_SLOT_COUNT];
  uint8_t sync_on_known[NOCTI_V4_SLOT_COUNT], sync_off_known[NOCTI_V4_SLOT_COUNT];
  uint32_t sync_on_planned[NOCTI_V4_SLOT_COUNT], sync_on_actual[NOCTI_V4_SLOT_COUNT];
  uint32_t sync_off_planned[NOCTI_V4_SLOT_COUNT], sync_off_actual[NOCTI_V4_SLOT_COUNT];
  uint8_t sync_anchor_valid, sync_frame_delta_valid;
  uint32_t sync_anchor, sync_frame_delta;
  struct {{ uint32_t slot_samples, off_phase_samples; }} sample_geometry;
  nocti_v4_health_t *health;
}} scheduler_runtime_t;
typedef struct {{ scheduler_runtime_t *runtime; }} nocti_v4_scheduler_t;
static nocti_v4_scheduler_t s_scheduler;
{chr(10).join(helpers)}
{formatter}
int main(void) {{
  /* Every unsigned counter at 0xFFFFFFFF (10 digits); signed fields at INT32_MIN / INT_MIN (11 chars). */
  memset(&s_health, 0xFF, sizeof s_health);
  s_health.async_callback_core = INT32_MIN;
  s_health.async_led_task_core = INT32_MIN;
  s_health.scheduler_task_core = INT32_MIN;
  memset(s_raw, 0xFF, sizeof *s_raw);
  memset(&s_net, 0xFF, sizeof s_net);
  s_net.last_errno = INT_MIN;
  s_net.frame_last_errno = INT_MIN;
  static scheduler_runtime_t r;
  memset(&r, 0xFF, sizeof r);
  for (unsigned i = 0; i < NOCTI_V4_SLOT_COUNT; ++i) {{
    r.sync_on_known[i] = 1; r.sync_off_known[i] = 1;
    r.sync_on_i2c_us[i] = 0xFFFFFFFFu; r.sync_off_i2c_us[i] = 0xFFFFFFFFu;
    r.sync_on_planned[i] = 0x80000000u; r.sync_on_actual[i] = 0u;   /* delta INT32_MIN */
    r.sync_off_planned[i] = 0x80000000u; r.sync_off_actual[i] = 0u;
  }}
  r.sync_anchor_valid = 1; r.sync_frame_delta_valid = 1;
  r.health = &s_health;
  s_scheduler.runtime = &r;
  static char text[4000];
  static char capped[READ_CAP];
  size_t n;
  (void)n;
{chr(10).join(calls)}
  return 0;
}}
"""
        return preamble

    def run_probe(self, code):
        path = self.work / "probe.c"
        path.write_text(code)
        exe = self.work / "probe"
        args = [self.cc, "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-function",
                "-Wno-unused-variable", str(path), "-o", str(exe)]
        args += ["-I" + str(p) for p in (HEALTH_INCLUDE, SCHED_INCLUDE, COMMON_INCLUDE)]
        built = subprocess.run(args, cwd=self.work, capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr[:3000])
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stderr)
        return ran.stdout.splitlines()

    def measure(self, states):
        lines = self.run_probe(self.build_probe(states))
        result = {}
        for index in range(0, len(lines), 3):
            tag, full = lines[index].split()
            capped_tag, returned = lines[index + 2].split()
            self.assertEqual(capped_tag, tag + "_CAPPED")
            result[tag] = {"full": int(full), "text": lines[index + 1], "returned": int(returned)}
        return result

    def assert_fits(self, tag, rec, limits):
        suffix = mode_suffix()
        print(f"{tag}: text={rec['full']} B, read cap={limits['read_cap']} B, "
              f"margin={limits['read_cap'] - rec['full']} B, "
              f"with mode suffix payload={limits['reply_prefix'] + rec['full'] + len(suffix)} B "
              f"of {limits['payload_max']} B")
        self.assertEqual(rec["full"], len(rec["text"]), tag + " length mismatch")
        self.assertLess(rec["full"], limits["read_cap"], tag + " exceeds diagnostic capacity")
        self.assertGreater(rec["returned"], 0, tag + " returns 0 at real capacity (fallback reply)")
        self.assertLessEqual(limits["reply_prefix"] + rec["full"] + len(suffix),
                             limits["payload_max"], tag + " payload with mode suffix overflows")

    def test_source_limits_are_the_documented_ones(self):
        self.assertEqual(source_limits(), {"payload_max": 1440, "diag_size": 1413,
                                           "read_cap": 1349, "reply_prefix": 28})
        self.assertEqual(len(mode_suffix()), 48)

    def test_netdiag_worst_case_fits(self):
        limits = source_limits()
        out = self.measure([("NETDIAG_IDLE", 3, IDLE), ("NETDIAG_RUNNING", 3, RUNNING)])
        for tag, rec in out.items():
            self.assert_fits(tag, rec, limits)
        self.assertIn("largest_free_block_now=4294967295", out["NETDIAG_IDLE"]["text"])
        self.assertIn("largest_free_block_now=na", out["NETDIAG_RUNNING"]["text"])
        self.assertIn("frame_last_errno=-2147483648", out["NETDIAG_IDLE"]["text"])

    @unittest.expectedFailure
    def test_health_worst_case_fits_known_over_limit(self):
        # Pre-Batch-4a content. Known to exceed the capacity at 10-digit worst case.
        self.assert_fits("HEALTH_IDLE", self.measure([("HEALTH_IDLE", 0, IDLE)])["HEALTH_IDLE"],
                         source_limits())

    @unittest.expectedFailure
    def test_timing_worst_case_fits_known_over_limit(self):
        # Pre-Batch-4a content. Known to exceed the capacity at 10-digit worst case.
        self.assert_fits("TIMING", self.measure([("TIMING", 1, IDLE)])["TIMING"], source_limits())


if __name__ == "__main__":
    unittest.main()
