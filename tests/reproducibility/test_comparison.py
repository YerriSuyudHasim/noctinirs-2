"""Host tooling tests; these do not constitute firmware/hardware unit evidence."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/reproducible_build.py"
spec = importlib.util.spec_from_file_location("repro", SCRIPT)
repro = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repro)
SHA = "a" * 40


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.a, self.b, self.report = [self.root / name for name in ("A", "B", "report")]
        for directory in (self.a, self.b):
            for name in repro.REQUIRED:
                path = directory / "bins" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"real unmodified fixture bytes: " + name.encode())
            (directory / "build.log").write_text("Build complete\n", encoding="utf-8")
            self.manifest(directory)

    def manifest(self, directory, **overrides):
        value = {
            "schema": 1, "complete": True, "errors": [],
            "artifacts": repro.inventory(directory / "bins"),
            "metadata": {
                "source_sha": SHA, "workspace": str(directory), "build_exit": 0,
                "image": repro.IMAGE, "idf_commit": repro.IDF_COMMIT,
                "compiler": repro.COMPILER, "warning_count": 0,
                "idf_version": "ESP-IDF v6.1", "cmake": "4.0.3", "ninja": "1.11.1",
                "python": "3.12.3", "sdkconfig_sha256": "b" * 64,
            },
        }
        value.update(overrides)
        repro.write_json(directory / "manifest.json", value)

    def mutate_metadata(self, **values):
        path = self.b / "manifest.json"
        data = json.loads(path.read_text())
        data["metadata"].update(values)
        repro.write_json(path, data)

    def compare(self, expected):
        with contextlib.redirect_stdout(io.StringIO()):
            actual = repro.compare(self.a, self.b, self.report)
        self.assertEqual(expected, actual)
        report = json.loads((self.report / "comparison.json").read_text())
        self.assertEqual("PASS" if expected else "FAIL", report["result"])
        self.assertTrue((self.report / "comparison.md").is_file())
        return report

    def test_identical_raw_artifacts_pass(self):
        self.compare(True)

    def test_content_change_same_size_fails_with_names_sizes_hashes_offsets_and_environment(self):
        path = self.b / "bins/noctinirs2.bin"
        data = path.read_bytes()
        path.write_bytes(bytes([data[0] ^ 1]) + data[1:])
        self.manifest(self.b)
        report = self.compare(False)
        row = next(r for r in report["artifacts"] if r["name"] == "noctinirs2.bin")
        self.assertEqual(row["A"]["size"], row["B"]["size"])
        self.assertNotEqual(row["A"]["sha256"], row["B"]["sha256"])
        self.assertEqual([0], row["first_different_offsets"])
        markdown = (self.report / "comparison.md").read_text()
        for fragment in ("noctinirs2.bin", row["A"]["sha256"], row["B"]["sha256"], "Environment A", "source_sha", repro.IMAGE):
            self.assertIn(fragment, markdown)

    def test_size_change_fails(self):
        with (self.b / "bins/noctinirs2.bin").open("ab") as stream:
            stream.write(b"appended")
        self.manifest(self.b)
        self.compare(False)

    def test_each_required_binary_missing_on_one_side_fails(self):
        for name in repro.REQUIRED:
            with self.subTest(name=name):
                path = self.b / "bins" / name
                data = path.read_bytes()
                path.unlink()
                self.manifest(self.b)
                self.assertIn(f"{name}: MISSING IN B", self.compare(False)["errors"])
                path.write_bytes(data)

    def test_required_binary_missing_from_both_builds_fails(self):
        for directory in (self.a, self.b):
            (directory / "bins/bootloader/bootloader.bin").unlink()
            self.manifest(directory)
        self.compare(False)

    def test_unexpected_nested_binary_fails(self):
        path = self.b / "bins/nested/extra/new.bin"
        path.parent.mkdir(parents=True)
        path.write_bytes(b"unexpected")
        self.manifest(self.b)
        self.assertIn("nested/extra/new.bin: UNEXPECTED IN B / MISSING IN A", self.compare(False)["errors"])

    def test_every_produced_binary_compared_even_if_not_a_flash_input(self):
        for directory in (self.a, self.b):
            path = directory / "bins/extra.bin"
            path.write_bytes(b"identical")
            self.manifest(directory)
        self.compare(True)
        (self.b / "bins/extra.bin").write_bytes(b"different")
        self.manifest(self.b)
        self.compare(False)

    def test_empty_binary_fails_even_when_identical(self):
        for directory in (self.a, self.b):
            (directory / "bins/noctinirs2.bin").write_bytes(b"")
            self.manifest(directory)
        self.compare(False)

    def test_incomplete_build_with_matching_partial_artifacts_fails(self):
        self.manifest(self.b, complete=False, errors=["idf.py build exited 2"])
        self.assertIn("B: idf.py build exited 2", self.compare(False)["errors"])

    def test_nonzero_build_exit_fails_even_with_all_binaries(self):
        self.mutate_metadata(build_exit=1)
        self.compare(False)

    def test_missing_manifest_fails_with_report(self):
        (self.b / "manifest.json").unlink()
        self.compare(False)

    def test_malformed_manifest_fails_with_report(self):
        (self.b / "manifest.json").write_text("not json")
        self.compare(False)

    def test_wrong_manifest_or_metadata_type_still_produces_failure_report(self):
        for value in ([], {"schema": 1, "complete": True, "metadata": []}):
            with self.subTest(value=value):
                repro.write_json(self.b / "manifest.json", value)
                self.compare(False)

    def test_post_capture_content_change_is_detected(self):
        (self.b / "bins/noctinirs2.bin").write_bytes(b"changed after capture")
        self.assertTrue(any("inventory differs" in e for e in self.compare(False)["errors"]))

    def test_post_capture_unexpected_bin_is_detected(self):
        (self.b / "bins/extra.bin").write_bytes(b"unrecorded")
        self.compare(False)

    def test_environment_changes_fail(self):
        baseline = (self.b / "manifest.json").read_text()
        for key, value in (("source_sha", "c" * 40), ("image", "espressif/idf:v6.1"),
                           ("idf_commit", "d" * 40), ("compiler", "other GCC"),
                           ("sdkconfig_sha256", "e" * 64), ("warning_count", 1)):
            with self.subTest(key=key):
                (self.b / "manifest.json").write_text(baseline)
                self.mutate_metadata(**{key: value})
                self.compare(False)

    def test_same_workspace_path_fails(self):
        self.mutate_metadata(workspace=str(self.a))
        self.compare(False)

    def test_no_binaries_in_both_builds_cannot_pass(self):
        for directory in (self.a, self.b):
            for path in (directory / "bins").rglob("*.bin"):
                path.unlink()
            self.manifest(directory)
        self.compare(False)

    def test_symlinked_binary_is_rejected(self):
        path = self.b / "bins/link.bin"
        try:
            path.symlink_to(self.b / "bins/noctinirs2.bin")
        except OSError:
            self.skipTest("host does not permit symlinks")
        self.compare(False)

    def test_cli_exit_status_and_report_survive_failure(self):
        args = [sys.executable, str(SCRIPT), "compare", str(self.a), str(self.b), "--output", str(self.report)]
        self.assertEqual(0, subprocess.run(args, capture_output=True).returncode)
        (self.b / "bins/noctinirs2.bin").unlink()
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(1, result.returncode)
        self.assertIn("MISSING IN B", result.stdout)
        self.assertIn("FAIL", (self.report / "comparison.md").read_text())


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.work, self.output = self.root / "work", self.root / "evidence"
        self.output.mkdir()
        (self.output / "build.log").write_text("Build complete\n")
        for name in repro.REQUIRED:
            path = self.work / "build" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"firmware bytes")
        for name in ("build/project_description.json", "build/compile_commands.json", "build/bootloader/compile_commands.json", "build/gdbinit/prefix_map"):
            path = self.work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("{}")
        (self.work / "sdkconfig.defaults").write_text("CONFIG_APP_REPRODUCIBLE_BUILD=y\n")
        (self.work / "sdkconfig").write_text('CONFIG_APP_REPRODUCIBLE_BUILD=y\nCONFIG_COMPILER_HIDE_PATHS_MACROS=y\nCONFIG_IDF_TARGET="esp32s3"\n')
        repro.write_json(self.work / "build/flasher_args.json", {"flash_files": {str(i): name for i, name in enumerate(sorted(repro.REQUIRED))}})
        def fake_command(*args):
            if args[0] == "git":
                if args[-1] == "--untracked-files=no":
                    return ""
                return SHA if args[2] == str(self.work) else repro.IDF_COMMIT
            return {"xtensa-esp32s3-elf-gcc": repro.COMPILER, "idf.py": "ESP-IDF v6.1", "cmake": "4.0.3", "ninja": "1.11.1"}[args[0]]
        self.command = patch.object(repro, "command", side_effect=fake_command)
        self.command.start()
        self.addCleanup(self.command.stop)
        self.env = patch.dict("os.environ", {"IDF_CONTAINER_IMAGE": repro.IMAGE, "IDF_PATH": "/idf"})
        self.env.start()
        self.addCleanup(self.env.stop)

    def snapshot(self, code=0):
        return repro.snapshot(self.work, self.output, SHA, code)

    def test_complete_snapshot_copies_exact_raw_bytes(self):
        self.assertTrue(self.snapshot())
        for name in repro.REQUIRED:
            self.assertEqual((self.work / "build" / name).read_bytes(), (self.output / "bins" / name).read_bytes())

    def test_incomplete_build_and_missing_metadata_recorded(self):
        (self.work / "build/flasher_args.json").unlink()
        self.assertFalse(self.snapshot(2))
        errors = json.loads((self.output / "manifest.json").read_text())["errors"]
        self.assertTrue(any("command exit 2" in e for e in errors))
        self.assertTrue(any("flasher_args.json" in e for e in errors))

    def test_unbuilt_flash_input_fails(self):
        repro.write_json(self.work / "build/flasher_args.json", {"flash_files": {"0x10000": "missing.bin"}})
        self.assertFalse(self.snapshot())

    def test_timestamp_enabled_config_fails(self):
        with (self.work / "sdkconfig").open("a") as stream:
            stream.write("CONFIG_APP_COMPILE_TIME_DATE=y\n")
        self.assertFalse(self.snapshot())

    def test_compiler_warning_fails(self):
        (self.output / "build.log").write_text("main.c:1: warning: example\n")
        self.assertFalse(self.snapshot())


class WorkflowContractTests(unittest.TestCase):
    def test_digest_and_head_sha_contract(self):
        workflow = (ROOT / ".github/workflows/reproducible-build.yml").read_text()
        self.assertEqual(2, workflow.count(repro.IMAGE))
        self.assertEqual(2, workflow.count("ref: ${{ github.event.pull_request.head.sha || github.sha }}"))
        self.assertIn("if: always()", workflow)
        self.assertIn("retention-days: 90", workflow)
        self.assertIn("include-hidden-files: true", workflow)
        self.assertNotIn("continue-on-error", workflow)
        self.assertIn(repro.IMAGE, (ROOT / ".github/workflows/firmware-build.yml").read_text())


if __name__ == "__main__":
    unittest.main()
