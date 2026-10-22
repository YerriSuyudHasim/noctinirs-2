"""AC1: a version mismatch must fail and a match must pass."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures"
CHECK = ROOT / "tools/check_version_consistency.py"
NOTES = ROOT / "tools/render_release_notes.py"
MAGIC = b"\x32\x54\xcd\xab"


def run_tool(script, args, env=None):
    clean = os.environ.copy()
    for name in ("GITHUB_REF_TYPE", "GITHUB_REF_NAME", "GITHUB_REF"):
        clean.pop(name, None)
    if env:
        clean.update(env)
    return subprocess.run(
        [sys.executable, str(script), *args],
        cwd=ROOT,
        env=clean,
        capture_output=True,
        text=True,
    )


def app_desc(version, copies=1):
    desc = bytearray(256)
    desc[0:4] = MAGIC
    raw = version.encode("ascii")
    desc[16:16 + len(raw)] = raw
    return b"\x00" * 32 + bytes(desc) * copies


class VersionFixtureTests(unittest.TestCase):
    def test_match_fixture_passes(self):
        result = run_tool(CHECK, ["--root", str(FIXTURES / "match")])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS version=0.1.0", result.stdout)

    def test_mismatch_fixture_fails(self):
        result = run_tool(CHECK, ["--root", str(FIXTURES / "mismatch")])
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("0.9.9", result.stdout)
        self.assertNotIn("PASS version=", result.stdout)

    def test_repository_tree_passes_without_a_tag(self):
        result = run_tool(CHECK, ["--root", str(ROOT)])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS version=0.1.0", result.stdout)
        self.assertIn("tag=not-checked", result.stdout)

    def test_missing_field_fails(self):
        for relative in (
            "VERSION",
            "CMakeLists.txt",
            "project.yaml",
            "components/nn2_core/include/nn2_core_types.h",
        ):
            with self.subTest(relative=relative):
                with tempfile.TemporaryDirectory() as folder:
                    root = Path(folder)
                    shutil.copytree(FIXTURES / "match", root, dirs_exist_ok=True)
                    (root / relative).unlink()
                    result = run_tool(CHECK, ["--root", str(root)])
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("missing", result.stdout)

    def test_single_field_drift_fails(self):
        edits = {
            "VERSION": "0.1.1\n",
            "project.yaml": 'development_version: "0.1.1"\n',
            "components/nn2_core/include/nn2_core_types.h": '#define NN2_VERSION_STRING "0.1.1"\n',
        }
        for relative, text in edits.items():
            with self.subTest(relative=relative):
                with tempfile.TemporaryDirectory() as folder:
                    root = Path(folder)
                    shutil.copytree(FIXTURES / "match", root, dirs_exist_ok=True)
                    (root / relative).write_text(text)
                    result = run_tool(CHECK, ["--root", str(root)])
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unquoted_yaml_and_duplicate_project_ver_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            shutil.copytree(FIXTURES / "match", root, dirs_exist_ok=True)
            (root / "project.yaml").write_text("development_version: 0.1.0\n")
            result = run_tool(CHECK, ["--root", str(root)])
            self.assertNotEqual(result.returncode, 0, result.stdout)
            (root / "project.yaml").write_text('development_version: "0.1.0"\n')
            cmake = root / "CMakeLists.txt"
            cmake.write_text(cmake.read_text() + 'set(PROJECT_VER "0.1.0")\n')
            result = run_tool(CHECK, ["--root", str(root)])
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("multiple", result.stdout)

    def test_version_txt_must_agree_when_present(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            shutil.copytree(FIXTURES / "match", root, dirs_exist_ok=True)
            (root / "version.txt").write_text("0.1.0\n")
            self.assertEqual(run_tool(CHECK, ["--root", str(root)]).returncode, 0)
            (root / "version.txt").write_text("9.9.9\n")
            result = run_tool(CHECK, ["--root", str(root)])
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("version.txt", result.stdout)

    def test_config_override_fails_and_comment_does_not(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            shutil.copytree(FIXTURES / "match", root, dirs_exist_ok=True)
            (root / "sdkconfig").write_text("# CONFIG_APP_PROJECT_VER_FROM_CONFIG is not set\n")
            self.assertEqual(run_tool(CHECK, ["--root", str(root)]).returncode, 0)
            (root / "sdkconfig").write_text("CONFIG_APP_EXCLUDE_PROJECT_VER_VAR=y\n")
            result = run_tool(CHECK, ["--root", str(root)])
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("CONFIG_APP_EXCLUDE_PROJECT_VER_VAR", result.stdout)

    def test_tag_matches_only_the_exact_version(self):
        root = str(FIXTURES / "match")
        match = run_tool(CHECK, ["--root", root, "--tag", "0.1.0"])
        self.assertEqual(match.returncode, 0, match.stdout + match.stderr)
        self.assertIn("tag=0.1.0", match.stdout)
        prefixed = run_tool(CHECK, ["--root", root, "--tag", "v0.1.0"])
        self.assertNotEqual(prefixed.returncode, 0)
        self.assertIn("v0.1.0", prefixed.stdout)
        from_env = run_tool(CHECK, ["--root", root], env={
            "GITHUB_REF_TYPE": "tag", "GITHUB_REF_NAME": "0.1.0"})
        self.assertEqual(from_env.returncode, 0, from_env.stdout + from_env.stderr)
        missing = run_tool(CHECK, ["--root", root], env={"GITHUB_REF_TYPE": "tag"})
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("GITHUB_REF_NAME", missing.stdout)
        conflict = run_tool(
            CHECK, ["--root", root, "--tag", "0.1.0"],
            env={"GITHUB_REF_TYPE": "tag", "GITHUB_REF_NAME": "0.1.1"})
        self.assertNotEqual(conflict.returncode, 0)

    def test_app_desc_bytes_must_match(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            shutil.copytree(FIXTURES / "match", root, dirs_exist_ok=True)
            good = root / "app.bin"
            good.write_bytes(app_desc("0.1.0"))
            result = run_tool(CHECK, ["--root", str(root), "--app-bin", str(good)])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("app_desc=0.1.0", result.stdout)
            bad = root / "bad.bin"
            bad.write_bytes(app_desc("0.2.0"))
            result = run_tool(CHECK, ["--root", str(root), "--app-bin", str(bad)])
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("0.2.0", result.stdout)
            ambiguous = root / "two.bin"
            ambiguous.write_bytes(app_desc("0.1.0", copies=2))
            result = run_tool(CHECK, ["--root", str(root), "--app-bin", str(ambiguous)])
            self.assertNotEqual(result.returncode, 0, result.stdout)
            empty = root / "empty.bin"
            empty.write_bytes(b"\x00" * 64)
            result = run_tool(CHECK, ["--root", str(root), "--app-bin", str(empty)])
            self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_workflow_runs_the_selftest_and_keeps_retention(self):
        workflow = (ROOT / ".github/workflows/version-consistency.yml").read_text()
        self.assertIn("tests/version-consistency", workflow)
        self.assertIn("retention-days: 90", workflow)
        self.assertNotIn("continue-on-error", workflow)
        firmware = (ROOT / ".github/workflows/firmware-build.yml").read_text()
        self.assertIn("tools/check_version_consistency.py", firmware)
        self.assertIn("--app-bin build/noctinirs2.bin", firmware)


class ReleaseNotesTests(unittest.TestCase):
    def test_version_section_is_rendered_without_neighbors(self):
        result = run_tool(NOTES, [
            "--changelog", str(FIXTURES / "changelog-ok/CHANGELOG.md"),
            "--version", "0.1.0"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Scaffold kept for the fixture.", result.stdout)
        self.assertNotIn("older section", result.stdout)
        self.assertNotIn("not a release section", result.stdout)

    def test_unreleased_changelog_and_missing_file_fail(self):
        unreleased = run_tool(NOTES, [
            "--changelog", str(FIXTURES / "changelog-unreleased/CHANGELOG.md"),
            "--version", "0.1.0"])
        self.assertNotEqual(unreleased.returncode, 0)
        self.assertIn("Unreleased", unreleased.stdout)
        missing = run_tool(NOTES, ["--changelog", str(FIXTURES / "missing-CHANGELOG.md"), "--version", "0.1.0"])
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("#42", missing.stdout)
        self.assertFalse((ROOT / "CHANGELOG.md").exists())


if __name__ == "__main__":
    unittest.main()
