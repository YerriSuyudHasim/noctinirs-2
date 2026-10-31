"""RAM budget contract (Batch 4a). Static checks and a SYNTHETIC linker map only.

No ESP-IDF build, no real Owner map and no target measurement is claimed here.
"""
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "ram_budget.json"
TOOL = ROOT / "tools" / "check_ram_budget.py"

spec = importlib.util.spec_from_file_location("check_ram_budget", TOOL)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def load_config():
    return json.loads(CONFIG.read_text(encoding="utf-8"))


def synthetic_map(bss_total=0x2A000, data_total=0x6268, extra=None, drop=None):
    """Minimal GNU-ld-style map. Each registered static has its own input section."""
    symbols = [  # (section, name, size, object)
        ("bss", "s_runtime", 88840, "libnocti_v4_optical.a(nocti_v4_optical.c.obj)"),
        ("bss", "s_runtime", 36152, "libnocti_v4_scheduler.a(nocti_v4_scheduler.c.obj)"),
        ("bss", "s_runtime", 18360, "libnocti_v4_ads.a(nocti_v4_ads.c.obj)"),
        ("bss", "s_runtime", 8104, "libnocti_v4_async_led.a(nocti_v4_async_led.c.obj)"),
        ("bss", "s_nn2_app", 4096, "libmain.a(main.c.obj)"),
        ("bss", "s_status", 28, "libnn2_net.a(nn2_net.c.obj)"),
        ("bss", "s_raw", 4, "libmain.a(main.c.obj)"),
    ]
    if extra:
        symbols.extend(extra)
    if drop:
        symbols = [item for item in symbols if item[1] != drop]
    lines = ["Memory Configuration", "", "Linker script and memory map", ""]
    lines.append(".dram0.data      0x3fc88000     0x%x" % 0x6268)
    lines.append(" .dram0.data.nn2_misc")
    lines.append("                0x3fc88000       0x40 libmain.a(main.c.obj)")
    lines.append(".dram0.bss")
    lines.append("                0x3fc8a000    0x%x" % bss_total)
    for section, name, size, obj in symbols:
        lines.append(" .%s.%s    0x3fc8a100    0x%x %s" % (section, name, size, obj))
    return "\n".join(lines) + "\n"


class ConfigMatchesCode(unittest.TestCase):
    def test_config_is_valid_and_limits_are_positive_integers(self):
        config = load_config()
        self.assertEqual(config["schema"], 1)
        for name in (".dram0.data", ".dram0.bss"):
            self.assertIsInstance(config["sections"][name]["limit_bytes"], int)
            self.assertGreater(config["sections"][name]["limit_bytes"], 0)
        labels = [entry["label"] for entry in config["symbols"]]
        self.assertEqual(len(labels), len(set(labels)))
        for entry in config["symbols"]:
            self.assertIsInstance(entry["limit_bytes"], int)
            self.assertGreater(entry["limit_bytes"], 0)

    def test_registered_declarations_exist_in_their_source(self):
        for entry in load_config()["symbols"]:
            source = (ROOT / entry["declared_in"]).read_text(encoding="utf-8", errors="replace")
            self.assertIn(entry["declaration"], source, entry["label"])

    def test_raw_buffer_constant_and_static_asserts_exist_and_match(self):
        config = load_config()
        header = (ROOT / "components/nn2_core/include/nn2_core_raw_buffer.h").read_text()
        match = re.search(r"#define NN2_RAW_BUFFER_MAX_BYTES\s+(\d+)u", header)
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1)), 6272)
        self.assertEqual(int(match.group(1)), config["heap"]["raw_buffer_heap_bytes"])
        for check in config["compile_time_checks"]:
            text = (ROOT / check["file"]).read_text(encoding="utf-8", errors="replace")
            self.assertIn(check["contains"], text, check["contains"])

    def test_net_stack_constant_matches_heap_requirement(self):
        config = load_config()
        rtos = (ROOT / "firmware/v4-idf61/components/nocti_v4_common/include/nocti_v4_rtos.h").read_text()
        match = re.search(r"#define NOCTI_V4_RTOS_STACK_NET_BYTES\s+(\d+)u", rtos)
        self.assertIsNotNone(match)
        heap = config["heap"]
        self.assertEqual(int(match.group(1)), heap["net_task_stack_bytes"])
        self.assertEqual(heap["requirement_bytes"],
                         heap["net_task_stack_bytes"] + heap["static_task_tcb_bytes"])

    def test_registered_static_sum_fits_bss_limit(self):
        config = load_config()
        total = sum(entry["limit_bytes"] for entry in config["symbols"])
        self.assertLessEqual(total, config["sections"][".dram0.bss"]["limit_bytes"])

    def test_heap_margins_are_proposals_with_values_in_range(self):
        heap = load_config()["heap"]
        self.assertGreater(heap["boot_largest_free_block_after_wifi_bytes"], heap["requirement_bytes"])
        self.assertGreaterEqual(heap["proposed_min_largest_block_margin_bytes"], 0)


class MapGate(unittest.TestCase):
    def setUp(self):
        self.config = load_config()

    def test_synthetic_map_within_limits_passes(self):
        config = dict(self.config)
        rows, failures = checker.check(synthetic_map(bss_total=172000), config)
        self.assertEqual(failures, [])
        self.assertTrue(any(name == "nocti_v4_optical.s_runtime" and used == 88840
                            for name, used, _ in rows))

    def test_bss_over_limit_fails(self):
        rows, failures = checker.check(synthetic_map(bss_total=172361), self.config)
        self.assertTrue(any(".dram0.bss 172361 B" in item for item in failures), failures)

    def test_symbol_over_limit_fails(self):
        config = json.loads(json.dumps(self.config))
        config["symbols"][5]["limit_bytes"] = 27  # s_status: one byte under the synthetic size
        _, failures = checker.check(synthetic_map(), config)
        self.assertTrue(any("nn2_net.s_status" in item and "> limit" in item for item in failures))

    def test_missing_symbol_fails_closed(self):
        _, failures = checker.check(synthetic_map(drop="s_raw"), self.config)
        self.assertTrue(any("symbol not found" in item and "s_raw" in item for item in failures), failures)

    def test_ambiguous_symbol_fails_closed(self):
        extra = [("bss", "s_runtime", 1, "libcopy_of_optical.a(nocti_v4_optical.c.obj)")]
        _, failures = checker.check(synthetic_map(extra=extra), self.config)
        self.assertTrue(any("ambiguous" in item for item in failures), failures)

    def test_map_without_dram_sections_is_an_error(self):
        with self.assertRaises(checker.MapError):
            checker.parse_map("Memory Configuration\n.text 0x40 0x10\n")

    def test_cli_exit_codes_for_pass_fail_and_bad_input(self):
        with tempfile.TemporaryDirectory() as scratch:
            ok = Path(scratch) / "ok.map"
            ok.write_text(synthetic_map(bss_total=172000))
            over = Path(scratch) / "over.map"
            over.write_text(synthetic_map(bss_total=172361))
            bad = Path(scratch) / "bad.map"
            bad.write_text("not a map")
            run = lambda *args: subprocess.run([sys.executable, str(TOOL), *map(str, args)],
                                               capture_output=True, text=True)
            self.assertEqual(run(ok).returncode, 0)
            self.assertEqual(run(over).returncode, 1)
            self.assertEqual(run(bad).returncode, 2)
            self.assertEqual(run(Path(scratch) / "missing.map").returncode, 2)


if __name__ == "__main__":
    unittest.main()
