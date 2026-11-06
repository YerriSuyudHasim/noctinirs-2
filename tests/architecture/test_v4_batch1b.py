"""Batch 1b host probes: production current conversion/limits and CLI behavior."""
import contextlib
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import test_v4_batch1 as probes

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import nn2ctl


class CurrentC(unittest.TestCase):
    def compile(self, code):
        cc = os.environ.get("NN2_BATCH1_CC") or shutil.which("cc") or shutil.which("cl")
        self.assertTrue(cc, "Native C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "probe.c"
            path.write_text(code)
            exe = Path(tmp) / "probe.exe"
            includes = [ROOT / "components/nn2_core/include", ROOT / "components/nn2_board/include"]
            if Path(cc).name.lower() in ("cl", "cl.exe"):
                args = [cc, "/nologo", "/std:c11", str(path), "/Fe:" + str(exe)] + ["/I" + str(p) for p in includes]
            else:
                args = [cc, "-std=c11", "-Wall", "-Wextra", str(path), "-o", str(exe)] + ["-I" + str(p) for p in includes]
            result = subprocess.run(args, cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_shared_pca_register_writes_and_rejection(self):
        source = (ROOT / "components/nn2_board/nn2_board_pca.c").read_text()
        self.compile(source + r'''
#include <assert.h>
static uint8_t registers[256];
static unsigned calls;
static nn2_status_t reset(void *ctx, bool asserted) { (void)ctx; (void)asserted; ++calls; return NN2_OK; }
static void delay(void *ctx, uint32_t us) { (void)ctx; (void)us; }
static nn2_status_t wr(void *ctx, const uint8_t *data, size_t n) {
 (void)ctx; ++calls; unsigned addr = data[0] & 0x7f;
 if (addr == IREFALL) memset(registers + IREF0, data[1], 24);
 else if (addr == PWMALL) memset(registers + 0x0a, data[1], 24);
 else for (size_t i=1; i<n; ++i) registers[addr+i-1] = data[i];
 return NN2_OK;
}
static nn2_status_t rd(void *ctx, uint8_t addr, uint8_t *data, size_t n) {
 (void)ctx; memcpy(data, registers + (addr & 0x7f), n); return NN2_OK;
}
int main(void) {
 assert(NN2_OPTICAL_DEFAULT_MA == 30 && NN2_OPTICAL_MAX_MA == 50);
 assert(NN2_OPTICAL_BRINGUP_MA == 30);
 const uint16_t ma[] = {0,15,30,50,51};
 const uint8_t codes[] = {0,66,133,222};
 nn2_board_pca_io_t io = {reset, delay, wr, rd, NULL};
 for (unsigned t=0; t<5; ++t) {
   uint16_t currents[20]; for (unsigned i=0; i<20; ++i) currents[i]=ma[t];
   nn2_board_pca_t chip; memset(registers,0,sizeof(registers)); calls=0;
   nn2_status_t result=nn2_board_pca_prepare(&chip,&io,currents);
   if(t==4) { assert(result==NN2_ERR_INVALID_ARG && calls==0); continue; }
   assert(result==NN2_OK);
   for(unsigned i=0;i<20;++i) assert(registers[IREF0+nn2_board_pca_channel(i)]==codes[t]);
   assert((uint32_t)ma[t]*(4u*NN2_BOARD_PCA_REXT_OHM)/900u <= UINT8_MAX);
 }
 return 0;
}
''')

    def test_v4_set_current_production_function_and_iref(self):
        source = (ROOT / "firmware/v4-idf61/components/nocti_v4_led/nocti_v4_led.c").read_text()
        setter = source[source.index("esp_err_t nocti_v4_led_set_iref_ma("):source.index("esp_err_t nocti_v4_led_reset_hold(")]
        self.compile(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "nn2_core_optical.h"
#define PCA_MAX_CURRENT_MA NN2_OPTICAL_MAX_MA
#define NOCTI_V4_PCA_REXT_OHM 1000u
#define PCA_IREF0 0x22u
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_RESPONSE 2
typedef int esp_err_t;
typedef struct { unsigned invalid_led_command, iref_mismatch, pca_readback_mismatch; } health_t;
typedef struct { bool initialized; health_t *health; uint8_t iref[24]; } nocti_v4_led_t;
static uint8_t written;
static unsigned writes;
static int remember(nocti_v4_led_t *p,int r) { (void)p; return r; }
static int fail_safe_reset(nocti_v4_led_t *p,int r) { (void)p; return r; }
static int nocti_v4_led_resolve_source(uint8_t s,uint8_t *c) { *c=s; return s<20 ? 0:1; }
static int write_reg(nocti_v4_led_t *p,uint8_t r,uint8_t v) { (void)p;(void)r; written=v; ++writes; return 0; }
static int read_reg(nocti_v4_led_t *p,uint8_t r,uint8_t *v) { (void)p;(void)r; *v=written; return 0; }
''' + probes.function(source, "current_to_iref") + setter + r'''
int main(void) {
 const uint16_t ma[]={0,15,30,50,51}; const uint8_t code[]={0,67,133,222};
 health_t health={0}; nocti_v4_led_t led={.initialized=true,.health=&health};
 for(unsigned i=0;i<5;++i) {
   writes=0; int r=nocti_v4_led_set_iref_ma(&led,0,ma[i]);
   if(i==4) assert(r==ESP_ERR_INVALID_ARG && writes==0);
   else { assert(r==ESP_OK && writes==1 && written==code[i]); assert(led.iref[0]==code[i]); }
 }
 assert(((uint64_t)50*1000*4*NOCTI_V4_PCA_REXT_OHM+450000)/900000 <= UINT8_MAX);
 return 0;
}
''')

    def test_info_uses_shared_current_constants(self):
        source = (ROOT / "firmware/v4-idf61/main/v4_nn2_control.inc").read_text()
        expression = source.split('.policy_info=', 1)[1].split('};', 1)[0]
        self.compile('#include <string.h>\n#include <assert.h>\n#include "nn2_core_optical.h"\n'
                     '#define NN2_CURRENT_TEXT_(value) #value\n'
                     '#define NN2_CURRENT_TEXT(value) NN2_CURRENT_TEXT_(value)\n'
                     'int main(void) { const char *info = ' + expression + ';\n'
                     'assert(strstr(info, "current_default_ma=30;current_max_ma=50"));\n'
                     'assert(strstr(info, "fs_allowed=2,4,5,8,10")); return 0; }\n')

    def test_scheduler_core_initialization_precedes_creation(self):
        source = (ROOT / "firmware/v4-idf61/components/nocti_v4_scheduler/nocti_v4_scheduler.c").read_text()
        self.assertLess(source.index("__atomic_store_n(&health->scheduler_task_core, -1"), source.index("xTaskCreateStaticPinnedToCore(scheduler_task"))
        task = probes.function(source, "scheduler_task")
        self.assertIn("runtime->task_core = xPortGetCoreID();", task)
        self.assertIn("&runtime->health->scheduler_task_core, runtime->task_core", task)


class CurrentCLI(unittest.TestCase):
    def run_cli(self, args, info="current_default_ma=30;current_max_ma=50", currents=None):
        def request(command, payload=None):
            return dict(error="OK", state="STREAMING" if command=="start" else "STOPPED",
                        info=info, current_ma=currents or [30]*20)
        with mock.patch.object(sys,"argv",["nn2ctl"]+args), mock.patch.object(nn2ctl,"Client") as factory:
            factory.return_value.request.side_effect=request
            out,err=io.StringIO(),io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err): result=nn2ctl.main()
            return result,err.getvalue(),factory

    def test_current_boundaries_and_warning(self):
        for ma in (0,15,30,50,51):
            result,err,factory=self.run_cli(["set-led-current","--current-ma",str(ma)])
            self.assertEqual(result, int(ma==51))
            if ma==51: factory.assert_not_called()
            else:
                factory.return_value.request.assert_any_call("set-led-current",struct.pack("<BH",0,ma))
                self.assertEqual("arus di atas default 30 mA" in err,ma>30)

    def test_advertised_limit_prevents_current_command(self):
        result,err,factory=self.run_cli(["set-led-current","--current-ma","50"],"current_default_ma=30;current_max_ma=30")
        self.assertEqual(result,1)
        self.assertIn("0..30",err)
        self.assertEqual([c.args[0] for c in factory.return_value.request.call_args_list],["info"])
        nn2ctl.validate_current(50,"legacy=1")

    def test_start_warns_for_existing_high_channel(self):
        result,err,factory=self.run_cli(["start","--led"],"led=REAL_BRINGUP;current_default_ma=30;current_max_ma=50",[30]*19+[50])
        self.assertEqual(result,0)
        self.assertIn("pastikan irradiansi sudah dihitung dan aman untuk kulit",err)
        factory.return_value.request.assert_any_call("get-config")
        factory.return_value.request.assert_any_call("start",None)

    def test_explicit_start_current_reaches_all_channels(self):
        result,err,factory=self.run_cli(["start","--led","--current-ma","50"],"led=REAL_BRINGUP;current_default_ma=30;current_max_ma=50")
        self.assertEqual(result,0)
        self.assertEqual(err.count("PERINGATAN"),1)
        for ch in range(20): factory.return_value.request.assert_any_call("set-led-current",struct.pack("<BH",ch,50))

if __name__ == "__main__": unittest.main()
