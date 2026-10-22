import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import memory_report as report
import collect_memory as collector


class MemoryTests(unittest.TestCase):
    def setUp(self):
        self.raw = report.load(Path(__file__).parent / "fixtures/size.json")
        self.provenance = {
            "source_sha": "4a21171353b72ceff62774ba114705e0c8be2c31",
            "idf_commit": collector.EXPECTED_IDF, "compiler": "esp-15.2.0_20251204 GCC 15.2.0",
            "target": "esp32s3", "size_tool_version": "2.3.2", "utc": "2026-10-17T09:00:00Z",
            "environment": "unit fixture (synthetic provenance)", "build_flags": "{}",
            "sdkconfig_values_sha256": "a" * 64, "report_sha256": "b" * 64, "map_sha256": "c" * 64}
        self.base = report.normalize(self.raw, self.provenance, 170048, 1048576)

    def changed(self, bytes):
        result = copy.deepcopy(self.base)
        result["provenance"]["source_sha"] = "d" * 40
        for key in ("dram_static", "diram"):
            old = result["metrics"][key]
            result["metrics"][key] = report.metric(old["used_bytes"] + bytes, old["capacity_bytes"])
        return result

    def test_actual_esp_idf_report(self):
        m = self.base["metrics"]
        self.assertEqual(m["iram_fixed"], report.metric(16384, 16384))
        self.assertEqual(m["iram_shared_text"]["used_bytes"], 32935)
        self.assertEqual(m["iram_placed"]["used_bytes"], 49319)
        self.assertEqual(m["dram_static"]["used_bytes"], 15307)
        self.assertEqual(m["diram"]["headroom_bytes"], 293518)

    def test_unchanged_with_different_firmware_sha(self):
        current = self.changed(0)
        result = report.compare(current, self.base, {})
        self.assertEqual(result["status"], "OK")
        self.assertEqual(result["metrics"]["dram_static"]["used_delta"], {"bytes": 0, "percent": 0.0})

    def test_increased_usage_and_headroom_delta(self):
        result = report.compare(self.changed(15307), self.base, {})["metrics"]
        self.assertEqual(result["dram_static"]["used_delta"], {"bytes": 15307, "percent": 100.0})
        self.assertEqual(result["diram"]["headroom_delta"]["bytes"], -15307)

    def test_decreased_usage(self):
        result = report.compare(self.changed(-1000), self.base, {})["metrics"]
        self.assertEqual(result["dram_static"]["used_delta"]["bytes"], -1000)
        self.assertAlmostEqual(result["dram_static"]["used_delta"]["percent"], -100000 / 15307)
        self.assertEqual(result["diram"]["headroom_delta"]["bytes"], 1000)

    def test_zero_baseline_delta(self):
        self.assertEqual(report.delta(0, 0), {"bytes": 0, "percent": 0.0})
        self.assertEqual(report.delta(1, 0), {"bytes": 1, "percent": None})
        self.assertEqual(report.delta(None, 0), {"bytes": None, "percent": None})

    def test_unknown_capacity_is_not_zero(self):
        item = self.base["metrics"]["flash_code"]
        self.assertIsNone(item["capacity_bytes"])
        self.assertIsNone(item["headroom_bytes"])
        self.assertIsNone(item["used_percent"])
        self.assertEqual(self.base["metrics"]["iram_fixed"]["headroom_bytes"], 0)

    def test_invalid_capacity(self):
        for value in (0, -1, None, True, 1.5):
            with self.subTest(value=value):
                bad = copy.deepcopy(self.raw)
                next(x for x in bad["layout"] if x["name"] == "IRAM")["total"] = value
                with self.assertRaises(report.InvalidData):
                    report.normalize(bad, self.provenance, 170048, 1048576)

    def test_invalid_usage_and_region_accounting(self):
        for key, value in (("used", -1), ("used", True), ("used", 1000000), ("free", 1)):
            with self.subTest(key=key, value=value):
                bad = copy.deepcopy(self.raw)
                next(x for x in bad["layout"] if x["name"] == "IRAM")[key] = value
                with self.assertRaises(report.InvalidData):
                    report.normalize(bad, self.provenance, 170048, 1048576)

    def test_missing_regions_and_sections(self):
        for region in ("IRAM", "DIRAM", "Flash Code", "Flash Data"):
            bad = copy.deepcopy(self.raw)
            bad["layout"] = [x for x in bad["layout"] if x["name"] != region]
            with self.assertRaises(report.InvalidData):
                report.normalize(bad, self.provenance, 170048, 1048576)
        bad = copy.deepcopy(self.raw)
        next(x for x in bad["layout"] if x["name"] == "DIRAM")["parts"].pop(".bss")
        with self.assertRaises(report.InvalidData):
            report.normalize(bad, self.provenance, 170048, 1048576)

    def test_duplicate_region_and_wrong_schema(self):
        bad = copy.deepcopy(self.raw)
        bad["layout"].append(bad["layout"][0])
        with self.assertRaises(report.InvalidData):
            report.normalize(bad, self.provenance, 170048, 1048576)
        bad["version"] = "future"
        with self.assertRaises(report.InvalidData):
            report.normalize(bad, self.provenance, 170048, 1048576)

    def test_new_memory_type_cannot_be_silently_omitted(self):
        bad = copy.deepcopy(self.raw)
        bad["layout"][0]["name"] = "DRAM"
        with self.assertRaises(report.InvalidData):
            report.normalize(bad, self.provenance, 170048, 1048576)

    def test_missing_baseline(self):
        result = report.compare(self.base, None, {})
        self.assertEqual(result["status"], "NO_BASELINE")
        self.assertIsNone(result["metrics"]["diram"]["used_delta"]["bytes"])

    def test_missing_provenance_or_metrics(self):
        for key in report.IDENTITY + ("source_sha", "map_sha256"):
            bad = copy.deepcopy(self.base)
            del bad["provenance"][key]
            with self.assertRaises(report.InvalidData):
                report.compare(bad, self.base, {})
        bad = copy.deepcopy(self.base)
        del bad["metrics"]["diram"]
        with self.assertRaises(report.InvalidData):
            report.compare(bad, self.base, {})

    def test_forged_derived_values(self):
        bad = copy.deepcopy(self.base)
        bad["metrics"]["diram"]["headroom_bytes"] += 1
        with self.assertRaises(report.InvalidData):
            report.compare(bad, self.base, {})

    def test_empty_image_or_missing_evidence_never_ok(self):
        for key, value in (("image_size_from_report_bytes", 0), ("evidence_class", None)):
            bad = copy.deepcopy(self.base)
            bad[key] = value
            with self.assertRaises(report.InvalidData):
                report.compare(bad, self.base, {})
        bad = copy.deepcopy(self.base)
        bad["metrics"]["flash_app"] = report.metric(0, 1048576)
        with self.assertRaises(report.InvalidData):
            report.compare(bad, self.base, {})

    def test_changed_toolchain_or_config_not_comparable(self):
        for key in report.IDENTITY:
            current = copy.deepcopy(self.base)
            current["provenance"][key] = "d" * (64 if "sha256" in key else 40) if key != "target" else "esp32"
            if key == "target":
                with self.assertRaises(report.InvalidData):
                    report.compare(current, self.base, {})
            else:
                self.assertEqual(report.compare(current, self.base, {})["status"], "NOT_COMPARABLE")

    def test_changed_capacity_not_comparable(self):
        current = copy.deepcopy(self.base)
        current["metrics"]["flash_app"] = report.metric(170048, 2097152)
        self.assertEqual(report.compare(current, self.base, {})["status"], "NOT_COMPARABLE")

    def test_warning_boundary_is_advisory(self):
        limits = {"dram_static": {"warning": {"max_increase_bytes": 100}}}
        self.assertEqual(report.compare(self.changed(100), self.base, limits)["status"], "OK")
        self.assertEqual(report.compare(self.changed(101), self.base, limits)["status"], "WARNING")

    def test_critical_and_headroom_warning(self):
        limits = {"diram": {"warning": {"min_headroom_bytes": 293500},
                            "critical": {"min_headroom_bytes": 293000}}}
        self.assertEqual(report.compare(self.changed(19), self.base, limits)["status"], "WARNING")
        self.assertEqual(report.compare(self.changed(519), self.base, limits)["status"], "CRITICAL")

    def test_percent_threshold(self):
        limits = {"flash_app": {"warning": {"max_used_percent": 16}}}
        self.assertEqual(report.compare(self.base, self.base, limits)["status"], "WARNING")

    def test_unavailable_threshold_never_ok(self):
        limits = {"flash_code": {"warning": {"max_used_percent": 50}}}
        self.assertEqual(report.compare(self.base, self.base, limits)["status"], "UNAVAILABLE")

    def test_invalid_thresholds(self):
        cases = [{"typo": {}}, {"diram": {"warn": {}}},
                 {"diram": {"warning": {"max_used_bytes": -1}}},
                 {"diram": {"warning": {"max_used_percent": 101}}},
                 {"diram": {"warning": {"max_used_bytes": float("nan")}}},
                 {"diram": {"warning": {"max_used_bytes": 10}, "critical": {"max_used_bytes": 5}}}]
        for limits in cases:
            with self.assertRaises(report.InvalidData):
                report.compare(self.base, self.base, limits)

    def test_malformed_missing_empty_duplicate_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "report.json"
            with self.assertRaises(report.InvalidData):
                report.load(p)
            for text in ("", "{", '{"a":1,"a":2}', '{"a":NaN}'):
                p.write_text(text, encoding="utf-8")
                with self.assertRaises(report.InvalidData):
                    report.load(p)

    def test_cli_invalid_writes_both_reports_and_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            result = subprocess.run([sys.executable, str(ROOT / "tools/memory_report.py"),
                                     "--current", str(root / "missing.json"), "--json", str(root / "out.json"),
                                     "--markdown", str(root / "out.md")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(report.load(root / "out.json")["status"], "INVALID")
            self.assertIn("INVALID", (root / "out.md").read_text())

    def test_cli_missing_baseline_path_not_ok(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            report.write(root / "current.json", self.base)
            rc = report.main(["--current", str(root / "current.json"), "--baseline", str(root / "missing.json"),
                              "--json", str(root / "out.json"), "--markdown", str(root / "out.md")])
            self.assertEqual(rc, 2)
            self.assertEqual(report.load(root / "out.json")["status"], "INVALID")

    def test_cli_warning_exit_zero_and_markdown(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            report.write(root / "current.json", self.changed(101))
            report.write(root / "baseline.json", self.base)
            report.write(root / "limits.json", {"dram_static": {"warning": {"max_increase_bytes": 100}}})
            rc = report.main(["--current", str(root / "current.json"), "--baseline", str(root / "baseline.json"),
                              "--thresholds", str(root / "limits.json"), "--json", str(root / "out.json"),
                              "--markdown", str(root / "out.md")])
            self.assertEqual(rc, 0)
            self.assertEqual(report.load(root / "out.json")["status"], "WARNING")
            text = (root / "out.md").read_text()
            self.assertIn("unavailable", text)
            self.assertIn("fragmentation", text)
            self.assertIn("4a211713", text)

    def test_partition_capacity(self):
        self.assertEqual(collector.partition_capacity("factory,app,factory,0x10000,1M,\n"), 1048576)
        self.assertEqual(collector.partition_capacity("ota0,app,ota_0,0x10000,0x80000,\nota1,app,ota_1,0x90000,1M,"), 524288)
        for text in ("", "nvs,data,nvs,0x9000,24K", "factory,app,factory,0x10000,0,", "bad"):
            with self.assertRaises(report.InvalidData):
                collector.partition_capacity(text)

    def test_invalid_app_sizes(self):
        for size, capacity in ((0, 1048576), (1, 0), (2000000, 1048576)):
            with self.assertRaises(report.InvalidData):
                report.normalize(self.raw, self.provenance, size, capacity)

    def test_config_values_hash_ignores_comment_timestamp(self):
        a = 'CONFIG_IDF_TARGET="esp32s3"\nCONFIG_X=y\n# Generated today\n'
        b = '# Generated tomorrow\nCONFIG_X=y\nCONFIG_IDF_TARGET="esp32s3"\n'
        self.assertEqual(collector.config_digest(a), collector.config_digest(b))
        self.assertEqual(collector.config_digest(a), collector.config_digest(a + "CONFIG_X=y\n"))
        self.assertNotEqual(collector.config_digest(a), collector.config_digest(a.replace("CONFIG_X=y", "CONFIG_X=n")))
        with self.assertRaises(report.InvalidData):
            collector.config_digest(a + "CONFIG_X=n\n")

    def test_failed_collection_keeps_failure_and_no_metrics(self):
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "project"
            project.mkdir()
            out = Path(tmp) / "evidence"
            failed = subprocess.CompletedProcess(["git"], 7, stdout="injected failure\n")
            with patch.object(collector.subprocess, "run", return_value=failed):
                with self.assertRaises(report.InvalidData):
                    collector.collect(project, out, "a" * 40)
            self.assertEqual(report.load(out / "failure.json")["status"], "INVALID")
            self.assertFalse((out / "metrics.json").exists())
            self.assertEqual(report.load(out / "commands.json")[0]["exit_code"], 7)

    def test_build_failure_cannot_be_masked_by_size_success(self):
        calls = []
        def fake_run(argv, **kwargs):
            calls.append(argv)
            output = ""
            rc = 0
            if argv[-2:] == ["rev-parse", "HEAD"]:
                output = collector.EXPECTED_IDF if "-C" in argv else self.provenance["source_sha"]
            elif "--version" in argv:
                output = "esp-15.2.0_20251204 GCC 15.2.0"
            elif argv[-1] == "build":
                output, rc = "injected build failure", 7
            return subprocess.CompletedProcess(argv, rc, stdout=output)
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp) / "project"
            project.mkdir()
            out = Path(tmp) / "evidence"
            with patch.dict(collector.os.environ, {"IDF_PATH": tmp}), patch.object(collector.subprocess, "run", side_effect=fake_run):
                with self.assertRaises(report.InvalidData):
                    collector.collect(project, out, self.provenance["source_sha"])
            self.assertEqual(report.load(out / "commands.json")[-1]["exit_code"], 7)
            self.assertFalse(any("size" in argv for argv in calls))
            self.assertFalse((out / "metrics.json").exists())


if __name__ == "__main__":
    unittest.main()
