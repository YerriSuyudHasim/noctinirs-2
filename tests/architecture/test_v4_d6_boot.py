"""Static checks for the v4-idf61 D6 init order, brownout pin, and IR notes.

Host/architecture only. No compiler, flash, or hardware.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LED = ROOT / "firmware/v4-idf61/components/nocti_v4_led/nocti_v4_led.c"
BOARD = ROOT / "firmware/v4-idf61/components/nocti_v4_board/nocti_v4_board.c"
ADS = ROOT / "firmware/v4-idf61/components/nocti_v4_ads/nocti_v4_ads.c"
DEFAULTS = ROOT / "firmware/v4-idf61/sdkconfig.defaults"
ADR3 = ROOT / "docs/adr/0003-rtos-task-topology.md"
ADR4 = ROOT / "docs/adr/0004-host-protocol.md"
BOOT = ROOT / "docs/V4_IDF61_BOOT_INIT.md"
OWNER = "6498933500"


def _function_body(source, name):
    match = re.search(
        r"(?:static )?esp_err_t %s\([^\)]*\)\n\{" % re.escape(name),
        source,
    )
    if match is None:
        raise AssertionError("missing function " + name)
    start = match.end()
    depth = 1
    for index, char in enumerate(source[start:], start):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    raise AssertionError("unclosed function " + name)


class V4D6Boot(unittest.TestCase):
    def test_d6_write_order_is_iref_then_pwmall_then_ledout(self):
        source = LED.read_text()
        body = _function_body(source, "write_d6_emitter_off")
        iref = body.find("PCA_IREFALL, 0x00u")
        pwm = body.find("PCA_PWMALL, 0x00u")
        ledout = body.find("PCA_LEDOUT0 + index), 0u")
        self.assertGreaterEqual(iref, 0)
        self.assertLess(iref, pwm)
        self.assertLess(pwm, ledout)
        golden = _function_body(source, "program_sync_golden_path")
        d6_call = golden.find("write_d6_emitter_off(led)")
        iref_zero = golden.find("verify_iref_zero(led)")
        ledout_check = golden.find("verify_all_off(led)")
        operating = golden.find("program_iref_verified(led)")
        self.assertGreaterEqual(d6_call, 0)
        self.assertLess(d6_call, iref_zero)
        self.assertLess(iref_zero, ledout_check)
        self.assertLess(ledout_check, operating)
        self.assertIn("#define PCA_MAX_CURRENT_MA          NN2_OPTICAL_MAX_MA", source)
        self.assertIn("#define PCA_DEFAULT_CURRENT_MA      NN2_OPTICAL_DEFAULT_MA", source)
        optical = (ROOT / "components/nn2_core/include/nn2_core_optical.h").read_text()
        self.assertRegex(optical, r"(?m)^#define NN2_OPTICAL_MAX_MA 50$")
        self.assertRegex(optical, r"(?m)^#define NN2_OPTICAL_DEFAULT_MA 30$")
        self.assertIn("#define PCA_IREFALL     0x40u", source)

    def test_runtime_off_uses_d6_write_order(self):
        source = LED.read_text()
        body = _function_body(source, "nocti_v4_led_all_off")
        d6 = body.find("write_d6_emitter_off(led)")
        iref = body.find("verify_iref_zero(led)")
        ledout = body.find("verify_all_off(led)")
        restore = body.find("program_iref_verified(led)")
        self.assertGreaterEqual(d6, 0)
        self.assertLess(d6, iref)
        self.assertLess(iref, ledout)
        self.assertLess(ledout, restore)
        self.assertNotIn("PCA_LEDOUT0 + index", body)
        off = _function_body(source, "nocti_v4_led_off")
        self.assertIn("nocti_v4_led_all_off(led)", off)
        on = _function_body(source, "nocti_v4_led_on")
        self.assertLess(on.find("nocti_v4_led_all_off(led)"), on.find("PCA_LEDOUT_ON"))
        qual = _function_body(source, "nocti_v4_led_run_fast_i2c_qualification")
        self.assertIn("nocti_v4_led_all_off(led)", qual)
        async_led = (ROOT / "firmware/v4-idf61/components/nocti_v4_async_led/nocti_v4_async_led.c").read_text()
        enter = _function_body(async_led, "enter_async")
        self.assertIn("nocti_v4_led_all_off(runtime->led)", enter)
        app = (ROOT / "firmware/v4-idf61/main/app_main.c").read_text()
        start = app.split("static esp_err_t scan_start(void)\n", 1)[1].split(
            "static esp_err_t scan_stop(void)\n", 1)[0]
        self.assertIn("nocti_v4_led_all_off(&s_led)", start)
        i2c = (ROOT / "firmware/v4-idf61/components/nocti_v4_i2c/nocti_v4_i2c.c").read_text()
        self.assertIn("i2c_master_transmit(", i2c)
        self.assertIn("i2c_master_transmit_receive(", i2c)

    def test_oe_stays_undriven(self):
        board = BOARD.read_text()
        self.assertIn("board->oe_state = NOCTI_V4_OE_PHYSICAL_STATE_UNVERIFIED;", board)
        self.assertNotIn("GPIO_NUM_26", board)
        self.assertNotRegex(board, r"OE.*GPIO_NUM_")

    def test_backlog_comment_matches_depth_8(self):
        ads = ADS.read_text()
        self.assertNotIn("Depth 16", ads)
        self.assertIn("NOCTI_V4_BACKLOG_FAULT_DEPTH (8)", ads)

    def test_brownout_is_the_idf_esp32s3_default(self):
        defaults = DEFAULTS.read_text()
        self.assertIn("CONFIG_ESP_BROWNOUT_DET=y", defaults)
        self.assertIn("CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7=y", defaults)
        for level in range(1, 7):
            self.assertNotIn(
                "CONFIG_ESP_BROWNOUT_DET_LVL_SEL_%d=y" % level, defaults)

    def test_adr_headers_cite_owner_authorization(self):
        adr3 = ADR3.read_text()
        adr4 = ADR4.read_text()
        header3 = adr3.split("## 1. Context", 1)[0]
        header4 = adr4.split("## 1. Context", 1)[0]
        self.assertIn("rev 3", header3)
        self.assertIn(OWNER, header3)
        self.assertIn("2026-10-29 23:08 KST", header3)
        self.assertIn("file rev 4", header4)
        self.assertIn(OWNER, header4)
        self.assertIn("2026-10-29 23:08 KST", header4)
        self.assertIn(OWNER, adr3[adr3.find("## 11. Revision history"):])
        self.assertIn(OWNER, adr4[adr4.find("| file rev 4 |"):])

    def test_boot_doc_records_d6_and_brownout(self):
        boot = BOOT.read_text()
        self.assertIn("6333972667", boot)
        self.assertIn("IREFALL", boot)
        self.assertIn("CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7=y", boot)
        self.assertIn("2.44", boot)
        self.assertIn("30 mA", boot)


if __name__ == "__main__":
    unittest.main()
