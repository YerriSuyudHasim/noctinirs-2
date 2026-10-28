"""Lightweight board-profile tests: config files and cmake -P only, no firmware build/device."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import nn2_board_profile as board


class BoardProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "config").mkdir()
        shutil.copyfile(ROOT / "config/board-profiles.json", self.root / "config/board-profiles.json")
        self.original = (b'# Owner configuration\r\nCONFIG_IDF_TARGET="esp32s3"\r\n'
                         b'CONFIG_CUSTOM_LIMIT=719\r\nCONFIG_NN2_LED_BRINGUP=y\r\n'
                         b'# CONFIG_NN2_LED_SHORT_BENCH is not set\r\n')
        (self.root / "sdkconfig").write_bytes(self.original)
        (self.root / ".vscode").mkdir()
        self.settings = {"idf.port": "KEEP", "idf.customExtraVars": {"KEEP": "value"},
                         "clangd.arguments": ["--keep"], "idf.cmakeCompilerArgs": ["-G", "Ninja", "-DKEEP=7"]}
        (self.root / ".vscode/settings.json").write_text(json.dumps(self.settings))

    def test_selection_preserves_unrelated_root_bytes_and_settings(self):
        result = board.select_profile(self.root, "main-board-short-advisory")
        text = (self.root / "sdkconfig").read_bytes()
        # Only declared fields may change, including the existing exact CRLF lines.
        self.assertIn(b'CONFIG_CUSTOM_LIMIT=719\r\n', text)
        self.assertIn(b'CONFIG_NN2_LED_SHORT_BENCH=y\r\n', text)
        settings = json.loads((self.root / ".vscode/settings.json").read_text())
        for key in ["idf.port", "idf.customExtraVars", "clangd.arguments"]:
            self.assertEqual(settings[key], self.settings[key])
        self.assertIn("-DKEEP=7", settings["idf.cmakeCompilerArgs"])
        self.assertIn("-DNN2_EXPECT_LED_POLICY=BENCH_WARN", settings["idf.cmakeCompilerArgs"])
        self.assertEqual(settings["idf.sdkconfigDefaults"][-1], "sdkconfig.led-short-bench")
        self.assertEqual(Path(result["backup"], "sdkconfig.before").read_bytes(), self.original)
        self.assertEqual(result["limit_s"], 0)
        self.assertFalse(result["single_run"])
        self.assertEqual(result["duration_control"], "HOST")
        self.assertEqual((result["fs_min"], result["fs_max"]), (2, 10))
        self.assertEqual(result["firmware_build"], "NOT RUN")
        self.assertEqual(result["runtime_i2c_hz"], 800000)
        self.assertIn(b'CONFIG_NN2_LED_RUNTIME_I2C_HZ=800000\r\n', text)
        self.assertIn(b'CONFIG_I2C_ISR_IRAM_SAFE=y\r\n', text)

    def test_selection_is_idempotent(self):
        board.select_profile(self.root, "main-board-short-advisory")
        before = (self.root / "sdkconfig").read_bytes()
        self.assertIsNone(board.select_profile(self.root, "main-board-short-advisory")["backup"])
        self.assertEqual((self.root / "sdkconfig").read_bytes(), before)

    def test_strict_switch_removes_advisory_expectation_and_retains_custom_args(self):
        board.select_profile(self.root, "main-board-short-advisory")
        board.select_profile(self.root, "main-board-strict")
        self.assertIn(b'# CONFIG_NN2_LED_SHORT_BENCH is not set', (self.root / "sdkconfig").read_bytes())
        settings = json.loads((self.root / ".vscode/settings.json").read_text())
        self.assertIn("-DNN2_EXPECT_LED_POLICY=STRICT", settings["idf.cmakeCompilerArgs"])
        self.assertNotIn("-DNN2_EXPECT_LED_POLICY=BENCH_WARN", settings["idf.cmakeCompilerArgs"])
        self.assertIn("-DKEEP=7", settings["idf.cmakeCompilerArgs"])
        self.assertEqual(settings["idf.sdkconfigDefaults"][-1], "sdkconfig.led-strict")

    def test_malformed_settings_does_not_touch_root(self):
        (self.root / ".vscode/settings.json").write_text('{ broken')
        with self.assertRaises(ValueError):
            board.select_profile(self.root, "main-board-short-advisory")
        self.assertEqual((self.root / "sdkconfig").read_bytes(), self.original)

    def test_wrong_target_and_duplicate_keys_are_rejected(self):
        profile = board.load_profile(self.root, "main-board-short-advisory")
        for content in ['CONFIG_IDF_TARGET="esp32"\n',
                        'CONFIG_NN2_LED_SHORT_BENCH=y\n# CONFIG_NN2_LED_SHORT_BENCH is not set\n']:
            with self.assertRaises(ValueError):
                board.patch_sdkconfig(content, profile)

    def test_missing_root_can_seed_explicit_target_without_building(self):
        profile = board.load_profile(self.root, "main-board-short-advisory")
        result = board.patch_sdkconfig("", profile)
        self.assertIn('CONFIG_IDF_TARGET="esp32s3"', result)
        self.assertIn('CONFIG_NN2_LED_SHORT_BENCH=y', result)

    def test_generated_strict_is_not_confused_with_new_advisory_root(self):
        board.select_profile(self.root, "main-board-short-advisory")
        directory = self.root / "build/config"
        directory.mkdir(exist_ok=True)
        config = {"NN2_PROFILE_COMMUNICATION": True, "NN2_LED_BRINGUP": True, "NN2_LED_SHORT_BENCH": False, "NN2_LED_RUNTIME_I2C_HZ": 400000, "I2C_ISR_IRAM_SAFE": True}
        (directory / "sdkconfig.json").write_text(json.dumps(config))
        with self.assertRaisesRegex(ValueError, "generated policy is STRICT"):
            board.check_generated(self.root, "main-board-short-advisory")
        self.assertEqual(board.check_generated(self.root, "main-board-strict")["generated_policy"], "STRICT")
        config["NN2_LED_SHORT_BENCH"] = True
        config["NN2_LED_RUNTIME_I2C_HZ"] = 800000
        (directory / "sdkconfig.json").write_text(json.dumps(config))
        self.assertEqual(board.check_generated(self.root, "main-board-short-advisory")["generated_policy"], "BENCH_WARN")

    def test_stale_clock_or_missing_iram_rejected_even_when_policy_matches(self):
        directory = self.root / "build/config"
        directory.mkdir(parents=True)
        config = {"NN2_PROFILE_COMMUNICATION": True, "NN2_LED_BRINGUP": True,
                  "NN2_LED_SHORT_BENCH": True, "NN2_LED_RUNTIME_I2C_HZ": 100000,
                  "I2C_ISR_IRAM_SAFE": True}
        for clock, iram in [(100000, True), (800000, False), (None, True)]:
            config.update(NN2_LED_RUNTIME_I2C_HZ=clock, I2C_ISR_IRAM_SAFE=iram)
            (directory / "sdkconfig.json").write_text(json.dumps(config))
            with self.assertRaisesRegex(ValueError, "clock/IRAM"):
                board.check_generated(self.root, "main-board-short-advisory")

    def test_cmake_transport_guard_without_firmware_configuration(self):
        cmake = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake unavailable; no firmware build attempted")
        for expected, actual, led, iram, success in [
                (800000, 800000, "y", "y", True), (400000, 400000, "y", "y", True),
                (800000, 100000, "y", "y", False), (800000, 400000, "y", "y", False),
                (800000, 800000, "", "y", False), (800000, 800000, "y", "", False)]:
            with self.subTest(expected=expected, actual=actual, iram=iram, led=led):
                script = self.root / "transport.cmake"
                script.write_text('include("' + (ROOT / "cmake/nn2_led_profile.cmake").as_posix() + '")\n' +
                                  f'nn2_check_led_transport("{expected}" "{actual}" "{led}" "{iram}")\n')
                result = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)

    def test_cmake_generated_policy_guard_without_project_or_compiler(self):
        cmake = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake unavailable; no firmware build attempted")
        for expected, comm, led, short, success in [
                ("BENCH_WARN", "y", "y", "y", True), ("STRICT", "y", "y", "", True),
                ("BENCH_WARN", "y", "y", "", False), ("STRICT", "y", "y", "y", False),
                ("BENCH_WARN", "y", "", "y", False), ("UNKNOWN", "y", "y", "y", False)]:
            with self.subTest(expected=expected, short=short, led=led):
                script = self.root / "guard.cmake"
                script.write_text('include("' + (ROOT / "cmake/nn2_led_profile.cmake").as_posix() + '")\n' +
                                  f'nn2_check_led_profile("{expected}" "{comm}" "{led}" "{short}")\n')
                result = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
