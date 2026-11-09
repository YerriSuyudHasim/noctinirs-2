"""Batch 5A: stop and fault observability, production-entry tests (host/unit and static).

What runs here is the production source, not a copy of it:
  * scan_stop() is extracted verbatim from firmware/v4-idf61/main/app_main.c and compiled
    natively with every stage function replaced by an injectable stub. All 512 combinations
    of stage failures, activity flags and communication config are executed and compared
    with an independent model of the baseline (bd880f5) call order and return chain.
  * control_start(), control_clear(), control_note_boot_fault() and control_diag_snapshot()
    are extracted from v4_nn2_control.inc and run against its real static declarations.
  * The Batch 5A write sites inside control_worker() and the kind-3 reader are checked as
    source structure (this worker needs FreeRTOS and cannot run natively).
  * The shutdown order and return chain of scan_stop() are pinned as text.
Evidence class: host/unit plus static. Nothing here runs on the target, measures timing,
proves locking behavior, or establishes anything about hardware.
"""
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "firmware/v4-idf61/main/app_main.c"
INC = ROOT / "firmware/v4-idf61/main/v4_nn2_control.inc"
HEADER = ROOT / "firmware/v4-idf61/components/nocti_v4_common/include/nocti_v4_stop_diag.h"
COMMON_INCLUDE = ROOT / "firmware/v4-idf61/components/nocti_v4_common/include"
CORE_INCLUDE = ROOT / "components/nn2_core/include"
PORTS_INCLUDE = ROOT / "components/nn2_ports/include"


def extract(source, signature_regex):
    """Return the text of one function definition whose signature matches the regex."""
    match = re.search(signature_regex + r"\s*\n\{", source)
    if not match:
        raise AssertionError("function not found: " + signature_regex)
    start = match.start()
    end, depth = source.index("{", match.start()) + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class Native(unittest.TestCase):
    def setUp(self):
        self.cc = os.environ.get("NN2_BATCH1_CC") or shutil.which("cc") or shutil.which("gcc")
        self.assertTrue(self.cc, "native C compiler required; a missing compiler is not PASS")
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.work = Path(tmp.name)

    def run_c(self, code, relax_unused=False):
        path = self.work / "probe.c"
        path.write_text(code)
        exe = self.work / ("probe.exe" if os.name == "nt" else "probe")
        args = [self.cc, "-std=c11", "-Wall", "-Wextra", str(path), "-o", str(exe)]
        if relax_unused:
            # The extracted static block declares variables that the five extracted functions
            # do not all use. This relaxes only the probe's own compile, never the product build.
            args += ["-Wno-unused-variable", "-Wno-unused-function"]
        args += ["-I" + str(p) for p in (COMMON_INCLUDE, CORE_INCLUDE, PORTS_INCLUDE)]
        built = subprocess.run(args, cwd=self.work, capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr)
        self.assertEqual(built.stderr.strip(), "", "probe must compile without warnings: " + built.stderr)
        ran = subprocess.run([str(exe)], cwd=self.work, capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        return ran.stdout


SCAN_STOP_PROBE = r"""
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nocti_v4_stop_diag.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_LOG_INFO 3
static int config_comm;
#define CONFIG_NOCTI_V4_NN2_COMMUNICATION config_comm
static int s_scheduler, s_optical, s_ads, s_network, s_async_led;
static int r_sched, r_ads, r_opt, r_frame, r_net, r_async, ads_active, async_active;
static char order[256];
static int frozen_frame, frozen_net;
static void rec(const char *tag) { strcat(order, tag); strcat(order, ","); }
static esp_err_t nocti_v4_scheduler_stop(int *p) { (void)p; rec("SCHED"); return r_sched; }
static void nocti_v4_optical_begin_stop(int *p) { (void)p; rec("BEGIN"); }
static bool nocti_v4_ads_is_active(int *p) { (void)p; rec("ADSACT"); return ads_active != 0; }
static esp_err_t nocti_v4_ads_stop_dark(int *p) { (void)p; rec("ADSSTOP"); return r_ads; }
static esp_err_t nocti_v4_optical_finish_stop(int *p) { (void)p; rec("FINISH"); return r_opt; }
static esp_err_t nocti_v4_optical_wait_frame_consumer_idle(int *p, unsigned ms)
{ (void)p; (void)ms; rec("FRAME"); return r_frame; }
static esp_err_t nocti_v4_network_end_scan_session(int *p, unsigned ms)
{ (void)p; (void)ms; rec("NET"); return r_net; }
static void control_allow_safe_restore(void) { rec("RESTORE"); }
static bool nocti_v4_async_led_is_active(int *p) { (void)p; rec("ASYNCACT"); return async_active != 0; }
static esp_err_t nocti_v4_async_led_exit(int *p) { (void)p; rec("ASYNCEXIT"); return r_async; }
static void freeze_scan_session_report(esp_err_t frame, esp_err_t net)
{ rec("FREEZE"); frozen_frame = frame; frozen_net = net; }
static void esp_log_level_set(const char *tag, int level) { (void)tag; (void)level; rec("LOG"); }

@@SCAN_STOP@@

/* Independent model of the baseline return chain (app_main.c at bd880f5, lines 242-247). */
static int baseline_return(int s, int a, int o, int l)
{
    if (s != 0) return s;
    if (a != 0) return a;
    if (o != 0) return o;
    return l;
}

int main(void)
{
    unsigned cases = 0u, fails = 0u;
    for (unsigned bits = 0u; bits < 64u; ++bits)
    for (int act_ads = 0; act_ads < 2; ++act_ads)
    for (int act_async = 0; act_async < 2; ++act_async)
    for (int comm = 0; comm < 2; ++comm) {
        r_sched = bits & 1u ? 0x101 : 0;  r_ads = bits & 2u ? 0x102 : 0;
        r_opt = bits & 4u ? 0x103 : 0;    r_async = bits & 8u ? 0x104 : 0;
        r_frame = bits & 16u ? 0x105 : 0; r_net = bits & 32u ? 0x106 : 0;
        ads_active = act_ads; async_active = act_async; config_comm = comm;
        /* Inactive stages are not called, so they contribute OK; the network drain is
         * skipped when the NN2 communication configuration owns the session. */
        const int ads_eff = act_ads ? r_ads : 0, async_eff = act_async ? r_async : 0;
        const int net_eff = comm ? 0 : r_net;
        char expected[256] = "SCHED,BEGIN,ADSACT,";
        if (act_ads) strcat(expected, "ADSSTOP,");
        strcat(expected, "FINISH,FRAME,");
        if (!comm) strcat(expected, "NET,");
        strcat(expected, "RESTORE,ASYNCACT,");
        if (act_async) strcat(expected, "ASYNCEXIT,");
        strcat(expected, "FREEZE,LOG,");
        const int want = baseline_return(r_sched, ads_eff, r_opt, async_eff);
        const uint8_t want_mask = (uint8_t)((r_sched ? 1u : 0u) | (ads_eff ? 2u : 0u) | (r_opt ? 4u : 0u) |
                                            (async_eff ? 8u : 0u) | (r_frame ? 16u : 0u) | (net_eff ? 32u : 0u));
        uint8_t want_stage = NOCTI_V4_STOP_STAGE_NONE;
        if (r_sched) want_stage = NOCTI_V4_STOP_STAGE_SCHEDULER;
        else if (ads_eff) want_stage = NOCTI_V4_STOP_STAGE_ADS;
        else if (r_opt) want_stage = NOCTI_V4_STOP_STAGE_OPTICAL;
        else if (async_eff) want_stage = NOCTI_V4_STOP_STAGE_ASYNC_LED;

        order[0] = '\0';
        nocti_v4_stop_report_t report = {0x7fff, 0xEE, 0xEE};
        const int got = scan_stop(&report);
        const int order_ok = strcmp(order, expected) == 0;
        const int freeze_ok = frozen_frame == r_frame && frozen_net == net_eff;
        order[0] = '\0';
        const int got_null = scan_stop(NULL);
        const int null_order_ok = strcmp(order, expected) == 0;
        ++cases;
        if (got != want || got_null != want || !order_ok || !null_order_ok || !freeze_ok ||
            report.error != want || report.stage != want_stage || report.mask != want_mask) {
            ++fails;
            printf("FAIL bits=%u ads=%d async=%d comm=%d got=%d null=%d want=%d order=%s expected=%s "
                   "stage=%u/%u mask=0x%02X/0x%02X freeze=%d\n", bits, act_ads, act_async, comm, got,
                   got_null, want, order, expected, report.stage, want_stage, report.mask, want_mask, freeze_ok);
        }
    }
    printf("CASES %u FAILS %u\n", cases, fails);
    return fails == 0u ? 0 : 1;
}
"""

CONTROL_PROBE = r"""
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nn2_port_optical.h"
#include "nocti_v4_stop_diag.h"
typedef int esp_err_t;
typedef void *TaskHandle_t;
typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
#define ESP_OK 0
#define ESP_FAIL (-1)
static unsigned lock_depth, lock_enters;
#define portENTER_CRITICAL(m) ((void)(m), ++lock_depth, ++lock_enters)
#define portEXIT_CRITICAL(m) ((void)(m), --lock_depth)
static unsigned notifies;
#define xTaskNotifyGive(h) ((void)(h), ++notifies)
@@STATICS@@
@@FUNCTIONS@@

static int failures;
#define CHECK(cond) do { if (!(cond)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void populate(void)
{
    s_control_diag.ctrl_error = 258; s_control_diag.stop_stage_error = 259;
    s_control_diag.ctrl_origin = NOCTI_V4_CTRL_ORIGIN_STOP;
    s_control_diag.failed_stop_stage = NOCTI_V4_STOP_STAGE_ADS;
    s_control_diag.stop_fail_mask = 0x12; s_control_diag.stop_attempts = 3; s_control_diag.stop_last_ok = 1;
}
static int diag_is_populated(void)
{
    return s_control_diag.ctrl_error == 258 && s_control_diag.stop_stage_error == 259 &&
           s_control_diag.ctrl_origin == NOCTI_V4_CTRL_ORIGIN_STOP &&
           s_control_diag.failed_stop_stage == NOCTI_V4_STOP_STAGE_ADS &&
           s_control_diag.stop_fail_mask == 0x12 && s_control_diag.stop_attempts == 3 &&
           s_control_diag.stop_last_ok == 1;
}
static int diag_is_empty(void)
{
    return s_control_diag.ctrl_error == 0 && s_control_diag.stop_stage_error == 0 &&
           s_control_diag.ctrl_origin == 0 && s_control_diag.failed_stop_stage == 0 &&
           s_control_diag.stop_fail_mask == 0 && s_control_diag.stop_attempts == 0 &&
           s_control_diag.stop_last_ok == 0;
}
static nn2_port_optical_config_t valid_config(void)
{
    nn2_port_optical_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.fs = 10u;
    return cfg;
}

int main(void)
{
    /* Boot state: static storage is empty. */
    CHECK(diag_is_empty());

    /* Boot fault is recorded with its origin and ESP_FAIL, keeps the sticky fault, and does not
     * overwrite an earlier first failure. */
    control_note_boot_fault();
    CHECK(s_control_diag.ctrl_origin == NOCTI_V4_CTRL_ORIGIN_BOOT && s_control_diag.ctrl_error == ESP_FAIL);
    CHECK(s_control_fault && s_control_state == NN2_OPTICAL_FAULT && lock_depth == 0u);
    memset(&s_control_diag, 0, sizeof s_control_diag);
    nocti_v4_stop_diag_note_ctrl(&s_control_diag, NOCTI_V4_CTRL_ORIGIN_START, 0x101);
    control_note_boot_fault();
    CHECK(s_control_diag.ctrl_origin == NOCTI_V4_CTRL_ORIGIN_START && s_control_diag.ctrl_error == 0x101);

    /* A latched failure means the sticky fault is set: START is refused and the record survives. */
    populate(); s_control_fault = true; s_control_state = NN2_OPTICAL_FAULT;
    nn2_port_optical_config_t cfg = valid_config();
    CHECK(control_start(NULL, &cfg) == NN2_ERR_INVALID_STATE);
    CHECK(diag_is_populated());
    CHECK(control_clear(NULL) == NN2_ERR_INVALID_STATE && s_control_state == NN2_OPTICAL_FAULT);
    CHECK(diag_is_populated());

    /* Invalid arguments are refused before any reset. */
    s_control_fault = false; s_control_state = NN2_OPTICAL_IDLE; s_control_working = false;
    s_control_stop = false; s_control_wanted = false;
    cfg.fs = 7u;
    CHECK(control_start(NULL, &cfg) == NN2_ERR_INVALID_ARG);
    CHECK(diag_is_populated());
    cfg = valid_config(); cfg.current_ma[3] = NN2_OPTICAL_MAX_MA + 1;
    CHECK(control_start(NULL, &cfg) == NN2_ERR_INVALID_ARG);
    CHECK(diag_is_populated());

    /* Refused for a busy state: no reset. */
    cfg = valid_config(); s_control_working = true;
    CHECK(control_start(NULL, &cfg) == NN2_ERR_INVALID_STATE);
    CHECK(diag_is_populated());

    /* An accepted START resets every per-session field, including the attempt counter. */
    s_control_working = false; notifies = 0u;
    CHECK(control_start(NULL, &cfg) == NN2_OK);
    CHECK(diag_is_empty() && s_control_state == NN2_OPTICAL_STARTING && notifies == 1u);
    CHECK(lock_depth == 0u);

    /* The snapshot is a copy of the record taken inside one critical section. */
    populate();
    nocti_v4_stop_diag_t copy; memset(&copy, 0xEE, sizeof copy);
    const unsigned before = lock_enters;
    control_diag_snapshot(&copy);
    CHECK(lock_enters == before + 1u && lock_depth == 0u);
    CHECK(copy.ctrl_error == 258 && copy.stop_stage_error == 259 && copy.stop_attempts == 3 &&
          copy.failed_stop_stage == NOCTI_V4_STOP_STAGE_ADS && copy.stop_fail_mask == 0x12);

    printf("FAILURES %d\n", failures);
    return failures == 0 ? 0 : 1;
}
"""


class ScanStopBehavior(Native):
    def test_extracted_scan_stop_all_512_combinations(self):
        app = APP.read_text()
        code = SCAN_STOP_PROBE.replace(
            "@@SCAN_STOP@@", extract(app, r"static esp_err_t scan_stop\(nocti_v4_stop_report_t \*report\)"))
        out = self.run_c(code)
        self.assertIn("CASES 512 FAILS 0", out)


class ControlFunctionBehavior(Native):
    def test_extracted_control_functions_reset_latch_and_refuse(self):
        inc = INC.read_text()
        statics = inc[inc.index("static TaskHandle_t s_control_worker"):inc.index("static const char *s_boot_stage")]
        functions = "\n".join(extract(inc, signature) for signature in (
            r"static void control_note_boot_fault\(void\)",
            r"static nn2_status_t control_start\(void \*ctx,const nn2_port_optical_config_t \*cfg\)",
            r"static uint8_t control_state\(void \*ctx\)",
            r"static void control_diag_snapshot\(nocti_v4_stop_diag_t \*out\)",
            r"static nn2_status_t control_clear\(void \*ctx\)"))
        code = CONTROL_PROBE.replace("@@STATICS@@", statics).replace("@@FUNCTIONS@@", functions)
        out = self.run_c(code, relax_unused=True)
        self.assertIn("FAILURES 0", out)


class SourceStructure(unittest.TestCase):
    def setUp(self):
        self.app = APP.read_text()
        self.inc = INC.read_text()
        self.scan_stop = extract(self.app, r"static esp_err_t scan_stop\(nocti_v4_stop_report_t \*report\)")
        self.worker = extract(self.inc, r"static void control_worker\(void \*ctx\)")

    def ordered(self, text, needles):
        position = -1
        for needle in needles:
            found = text.find(needle, position + 1)
            self.assertGreater(found, position, "missing or out of order: " + needle)
            position = found

    def test_scan_stop_order_and_return_chain_are_the_baseline_text(self):
        self.ordered(self.scan_stop, [
            "nocti_v4_scheduler_stop(&s_scheduler)",
            "nocti_v4_optical_begin_stop(&s_optical)",
            "nocti_v4_ads_is_active(&s_ads) ?",
            "nocti_v4_ads_stop_dark(&s_ads)",
            "nocti_v4_optical_finish_stop(&s_optical)",
            "nocti_v4_optical_wait_frame_consumer_idle(&s_optical, 500u)",
            "nocti_v4_network_end_scan_session(&s_network, 500u)",
            "control_allow_safe_restore();",
            "nocti_v4_async_led_is_active(&s_async_led) ?",
            "nocti_v4_async_led_exit(&s_async_led)",
            "freeze_scan_session_report(frame_drain_result, network_drain_result);",
            'esp_log_level_set("*", ESP_LOG_INFO);',
            "nocti_v4_stop_report_make(",
            "if (scheduler_result != ESP_OK) return scheduler_result;"])
        chain = ("    if (scheduler_result != ESP_OK) return scheduler_result;\n"
                 "    if (ads_result != ESP_OK) return ads_result;\n"
                 "    if (optical_result != ESP_OK) return optical_result;\n")
        self.assertIn(chain, self.scan_stop)
        self.assertTrue(self.scan_stop.rstrip().endswith("return async_result;\n}"))
        self.assertEqual(self.scan_stop.count("return "), 4, "no new return path")

    def test_reporting_is_the_only_addition_inside_scan_stop(self):
        before_chain = self.scan_stop[:self.scan_stop.index("    if (scheduler_result != ESP_OK)")]
        added = before_chain[before_chain.index("    if (report != NULL) {"):]
        self.assertEqual(added.count(";"), 1, "the report block is a single call")
        self.assertIn("nocti_v4_stop_report_make(report, scheduler_result, ads_result, optical_result,", added)

    def test_worker_start_failure_site_latches_inside_the_existing_critical_section(self):
        site = self.worker[self.worker.index("bool cancelled="):]
        site = site[:site.index("s_control_working=false;")]
        self.assertIn("if (!cancelled) {", site)
        self.ordered(site, ["s_control_fault=true; s_control_error=result;",
                            "nocti_v4_stop_diag_note_ctrl(&s_control_diag,NOCTI_V4_CTRL_ORIGIN_START,result);",
                            "s_control_wanted=false;"])

    def test_worker_runtime_monitor_records_component_and_error_before_control_off(self):
        site = self.worker[self.worker.index("sched.lifecycle==NOCTI_V4_SCAN_FAULTED ||"):]
        site = site[:site.index("s_control_state=NN2_OPTICAL_FAULT;")]
        self.ordered(site, ["portENTER_CRITICAL(&s_control_lock);", "s_control_fault=true;",
                            "if (sched.lifecycle==NOCTI_V4_SCAN_FAULTED)",
                            "NOCTI_V4_CTRL_ORIGIN_RUNTIME_SCHED,sched.last_error",
                            "else", "NOCTI_V4_CTRL_ORIGIN_RUNTIME_ADS,adc.last_error",
                            "portEXIT_CRITICAL(&s_control_lock);", "(void)control_off(NULL);"])

    def test_worker_stop_site_commits_once_per_scan_stop_and_keeps_fault_handling(self):
        self.assertEqual(len(re.findall(r"\bscan_stop\(&", self.inc)), 1, "only one scan_stop() invocation site")
        self.assertEqual(self.inc.count("nocti_v4_stop_diag_commit("), 1)
        site = self.worker[self.worker.index("nocti_v4_stop_report_t stop_report"):]
        site = site[:site.index("s_control_state=s_control_fault ?")]
        self.ordered(site, [
            "esp_err_t result=scan_stop(&stop_report);",
            "portENTER_CRITICAL(&s_control_lock);",
            "(void)nocti_v4_board_set_emitter_gate(&s_board,false);",
            "nocti_v4_stop_diag_commit(&s_control_diag,&stop_report);",
            "if (result!=ESP_OK) { s_control_fault=true; s_control_error=result; }",
            "s_control_working=false; s_control_clean=result==ESP_OK;"])

    def test_every_diag_write_is_inside_a_control_lock_critical_section(self):
        # The only functions allowed to touch s_control_diag are these; each must hold the lock.
        writers = {
            "control_note_boot_fault": r"static void control_note_boot_fault\(void\)",
            "control_start": r"static nn2_status_t control_start\(void \*ctx,const nn2_port_optical_config_t \*cfg\)",
            "control_diag_snapshot": r"static void control_diag_snapshot\(nocti_v4_stop_diag_t \*out\)",
            "control_worker": r"static void control_worker\(void \*ctx\)",
        }
        owners = set()
        for name, signature in writers.items():
            body = extract(self.inc, signature)
            if "s_control_diag" not in body:
                continue
            owners.add(name)
            for hit in re.finditer(r"\bs_control_diag\b", body):
                before = body[:hit.start()]
                self.assertGreater(before.count("portENTER_CRITICAL"), before.count("portEXIT_CRITICAL"),
                                   name + ": diagnostic access outside the control lock near: " +
                                   body[max(0, hit.start() - 60):hit.end() + 20])
        self.assertEqual(owners, set(writers))
        declared = len(re.findall(r"\bs_control_diag\b", self.inc))
        inside = sum(len(re.findall(r"\bs_control_diag\b", extract(self.inc, s))) for s in writers.values())
        self.assertEqual(declared, inside + 1, "s_control_diag is declared once and used only by the four owners")

    def test_netdiag_reader_appends_the_snapshot_and_nothing_else_moves(self):
        reader = extract(self.inc, r"static size_t control_read_diag\(void \*ctx, uint8_t kind, uint8_t \*out, size_t cap\)")
        kind3 = reader[reader.index("if (kind == 3u) {"):reader.index("nocti_v4_task_wdt_log_pending();")]
        self.ordered(kind3, ["int netdiag = snprintf(", "control_diag_snapshot(&diag);",
                             "nocti_v4_stop_diag_format(&diag", "return netdiag > 0 && (size_t)netdiag < cap"])
        for key in ("ctrl_origin", "failed_stop_stage", "stop_attempts", "stop_fail_mask"):
            self.assertNotIn(key, reader, "keys are produced by the header, not by the reader")

    def test_health_and_timing_formatter_code_is_unchanged_from_the_baseline(self):
        reader = extract(self.inc, r"static size_t control_read_diag\(void \*ctx, uint8_t kind, uint8_t \*out, size_t cap\)")
        kind3_start = reader.index("    if (kind == 3u) {")
        health_timing = reader[reader.index("    nocti_v4_task_wdt_log_pending();"):]
        prologue = reader[:kind3_start]
        digest = hashlib.sha256((prologue + health_timing).encode()).hexdigest()
        # sha256 of the baseline (bd880f5) control_read_diag with its kind-3 block removed.
        self.assertEqual(digest, "84e6bbd8a047e9c60c067c5fd859010959c1063d2d9b59fb7e18bb678c9690a1")


NETDIAG_PREFIX = ("v4netdiag=1 heap_free_now=20332 heap_free_min=11276 largest_free_block_now=7680 "
                  "frame_send_errors=0 frame_send_eagain=0 frame_send_enobufs=0 frame_send_other=0 "
                  "frame_last_errno=0 net_received=10 net_replied=10 net_invalid=0 net_send_errors=0 "
                  "net_last_errno=0")

HOST_TOOL_PROBE = r"""
#include <stdio.h>
#include <string.h>
#include "nocti_v4_stop_diag.h"
int main(void)
{
    nocti_v4_stop_diag_t d; char text[256];
    nocti_v4_stop_diag_reset(&d);
    nocti_v4_stop_report_t ok; nocti_v4_stop_report_make(&ok, 0, 0, 0, 0, 0, 0);
    nocti_v4_stop_diag_commit(&d, &ok);
    nocti_v4_stop_diag_format(&d, text, sizeof text); puts(text);
    nocti_v4_stop_diag_reset(&d);
    nocti_v4_stop_report_t bad; nocti_v4_stop_report_make(&bad, 0, 258, 0, 259, 261, 0);
    nocti_v4_stop_diag_commit(&d, &bad);
    nocti_v4_stop_diag_format(&d, text, sizeof text); puts(text);
    return 0;
}
"""


class HostToolCompatibility(Native):
    def test_nn2ctl_parses_the_appended_keys_and_keeps_the_observation_decoder_working(self):
        import sys
        sys.path.insert(0, str(ROOT / "tools"))
        import nn2ctl
        clean, failed = self.run_c(HOST_TOOL_PROBE).splitlines()
        fields = nn2ctl.parse_diag(NETDIAG_PREFIX + clean)
        self.assertEqual((fields["ctrl_origin"], fields["failed_stop_stage"], fields["stop_fail_mask"],
                          fields["stop_attempts"], fields["stop_last_ok"], fields["stop_other_codes"]),
                         ("NONE", "NONE", "0x00", "1", "1", "none"))
        fields = nn2ctl.parse_diag(NETDIAG_PREFIX + failed)
        self.assertEqual((fields["ctrl_origin"], fields["ctrl_error"], fields["failed_stop_stage"],
                          fields["stop_stage_error"], fields["stop_fail_mask"], fields["stop_last_ok"],
                          fields["stop_other_codes"]),
                         ("STOP", "258", "ADS", "258", "0x1A", "0", "unavailable"))
        self.assertEqual(nn2ctl.parse_observation(fields)["heap_free_now"], 20332)


class HeaderContract(unittest.TestCase):
    def test_header_has_no_platform_dependency_and_keeps_a_16_byte_record(self):
        text = HEADER.read_text()
        code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)  # comments may name the platform
        for forbidden in ("esp_", "freertos", "FreeRTOS", "portENTER", "heap_caps", "malloc", "calloc"):
            self.assertNotIn(forbidden, code)
        self.assertIn('_Static_assert(sizeof(nocti_v4_stop_diag_t) == 16u', text)
        includes = re.findall(r"#include <([^>]+)>", text)
        self.assertEqual(sorted(includes), ["inttypes.h", "stddef.h", "stdint.h", "stdio.h"])


if __name__ == "__main__":
    unittest.main()
