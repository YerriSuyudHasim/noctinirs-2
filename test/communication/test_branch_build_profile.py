"""Exercise branch checkout/profile isolation and generated-config rejection without hardware."""
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


class BranchBuildProfileTests(unittest.TestCase):
    def setUp(self):
        self.cmake = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if not self.cmake:
            self.skipTest("CMake unavailable")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "config").mkdir()
        shutil.copyfile(ROOT / "config/active-build-profile.cmake",
                        self.root / "config/active-build-profile.cmake")
        for overlay in ("sdkconfig.defaults", "sdkconfig.communication", "sdkconfig.led-bringup",
                        "sdkconfig.led-short-bench", "sdkconfig.acquisition"):
            shutil.copyfile(ROOT / overlay, self.root / overlay)
        self.old = b"# Old LED configuration\nCONFIG_NN2_LED_BRINGUP=y\n"
        (self.root / "sdkconfig").write_bytes(self.old)

    def run_cmake(self, text):
        script = self.root / "test.cmake"
        script.write_text('set(CMAKE_SOURCE_DIR "' + self.root.as_posix() + '")\n' +
                          'include("' + (ROOT / "cmake/nn2_branch_profile.cmake").as_posix() + '")\n' +
                          text, encoding="utf-8")
        return subprocess.run([self.cmake, "-P", str(script)], capture_output=True, text=True)

    def test_old_led_config_and_cache_cannot_select_acquisition_profile(self):
        result = self.run_cmake('set(NN2_USE_BRANCH_PROFILE ON)\n'
                                'set(SDKCONFIG "sdkconfig" CACHE FILEPATH "")\n'
                                'set(SDKCONFIG_DEFAULTS "sdkconfig.fake" CACHE STRING "")\n'
                                'nn2_prepare_branch_profile()\n'
                                'file(WRITE "${CMAKE_SOURCE_DIR}/selected.txt" "${SDKCONFIG}\n${SDKCONFIG_DEFAULTS}")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        selected = (self.root / "selected.txt").read_text()
        self.assertIn("build/profile-configs/ads131-acquisition/sdkconfig", selected)
        self.assertIn("sdkconfig.acquisition", selected)
        self.assertNotIn("sdkconfig.fake", selected)
        self.assertEqual((self.root / "sdkconfig").read_bytes(), self.old)

    def test_versioned_profile_switch_selects_another_config(self):
        path = self.root / "config/active-build-profile.cmake"
        path.write_text('set(NN2_BRANCH_PROFILE "led-checkpoint")\n'
                        'set(NN2_BRANCH_DEFAULTS sdkconfig.defaults sdkconfig.communication sdkconfig.led-bringup)\n')
        result = self.run_cmake('set(NN2_USE_BRANCH_PROFILE ON)\n'
                                'nn2_prepare_branch_profile()\nmessage(STATUS "selected=${SDKCONFIG}")\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("profile-configs/led-checkpoint/sdkconfig", result.stdout)
        self.assertNotIn("profile-configs/ads131-acquisition/sdkconfig", result.stdout)

    def test_explicit_matrix_profile_is_preserved_when_disabled(self):
        result = self.run_cmake('set(NN2_USE_BRANCH_PROFILE OFF)\n'
                                'set(SDKCONFIG_DEFAULTS sdkconfig.fake)\n'
                                'nn2_prepare_branch_profile()\nnn2_validate_branch_profile()\n'
                                'if(NOT SDKCONFIG_DEFAULTS STREQUAL "sdkconfig.fake")\nmessage(FATAL_ERROR "overridden")\nendif()\n')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_stale_or_incompatible_generated_config_is_rejected(self):
        prefix = ('set(NN2_USE_BRANCH_PROFILE ON)\nnn2_prepare_branch_profile()\n'
                  'foreach(key IN LISTS NN2_BRANCH_REQUIRED_ON)\nset(CONFIG_${key} y)\nendforeach()\n'
                  'set(CONFIG_NN2_LED_RUNTIME_I2C_HZ 800000)\n')
        for mutation, diagnostic in [
                ("", ""),
                ('set(CONFIG_NN2_OPTICAL_ACQUISITION "")\n', "CONFIG_NN2_OPTICAL_ACQUISITION=y"),
                ('set(CONFIG_SPIRAM y)\n', "CONFIG_SPIRAM disabled"),
                ('set(CONFIG_NN2_LED_RUNTIME_I2C_HZ 400000)\n', "clock mismatch")]:
            with self.subTest(mutation=mutation):
                result = self.run_cmake(prefix + mutation + "nn2_validate_branch_profile()\n")
                self.assertEqual(result.returncode == 0, not diagnostic, result.stdout + result.stderr)
                if diagnostic:
                    self.assertIn(diagnostic, result.stderr)

    def test_wrong_target_is_rejected(self):
        result = self.run_cmake('set(NN2_USE_BRANCH_PROFILE ON)\nset(IDF_TARGET esp32)\n'
                                'nn2_prepare_branch_profile()\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires ESP32-S3", result.stderr)

    def test_editor_build_and_generated_config_paths_match_profile(self):
        settings = json.loads((ROOT / ".vscode/settings.json").read_text())
        self.assertIn("-DNN2_USE_BRANCH_PROFILE=ON", settings["idf.cmakeCompilerArgs"])
        self.assertEqual(settings["idf.buildPath"], "${workspaceFolder}/build/ads131-acquisition")
        self.assertEqual(settings["idf.sdkconfigFilePath"],
                         "${workspaceFolder}/build/profile-configs/ads131-acquisition/sdkconfig")
        self.assertEqual(settings["idf.sdkconfigDefaults"][-1], "sdkconfig.acquisition")

    def test_manual_led_selector_explicitly_disables_branch_default(self):
        existing = {"idf.cmakeCompilerArgs": ["-DKEEP=1", "-DNN2_USE_BRANCH_PROFILE=ON"]}
        profile = board.load_profile(ROOT, "main-board-short-advisory")
        args = board.workspace_settings(existing, profile)["idf.cmakeCompilerArgs"]
        self.assertIn("-DKEEP=1", args)
        self.assertIn("-DNN2_USE_BRANCH_PROFILE=OFF", args)
        self.assertNotIn("-DNN2_USE_BRANCH_PROFILE=ON", args)


if __name__ == "__main__":
    unittest.main()
