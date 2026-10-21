"""Exercise the actual shell orchestration with failing tools, no SDK or network."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RunnerTests(unittest.TestCase):
    def test_failed_build_cannot_be_hidden_by_successful_size_command(self):
        bash = shutil.which("bash")
        if not bash:
            self.skipTest("Bash is required for the orchestration fixture")
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, idf, output = root / "source", root / "idf", root / "evidence"
            (source / "tools").mkdir(parents=True)
            idf.mkdir()
            shutil.copyfile(ROOT / "tools/build_twice.sh", source / "tools/build_twice.sh")
            # The capture/comparison helper is a spy here; comparator semantics
            # are tested separately against real raw binaries in test_comparison.
            (source / "tools/reproducible_build.py").write_text(
                'import json, pathlib, sys\n'
                'args = sys.argv\n'
                'if args[1] == "snapshot":\n'
                '    path = pathlib.Path(args[args.index("--output") + 1])\n'
                '    record = {"build_exit": int(args[args.index("--build-exit") + 1]),\n'
                '              "workspace": args[args.index("--workspace") + 1]}\n'
                '    (path / "spy.json").write_text(json.dumps(record))\n'
                '    sys.exit(1 if record["build_exit"] else 0)\n'
                'sys.exit(1)\n', encoding="utf-8")
            (idf / "export.sh").write_text('export PATH="$IDF_PATH:$PATH"\n', encoding="utf-8", newline="\n")
            tool = idf / "idf.py"
            tool.write_text(
                '#!/usr/bin/env bash\n'
                'case "$1" in\n'
                ' set-target) echo "target configured" ;;\n'
                ' build) echo "injected build failure"; exit 7 ;;\n'
                ' size) echo "size would succeed"; echo reached > "$FAKE_SIZE_REACHED" ;;\n'
                'esac\n', encoding="utf-8", newline="\n")
            tool.chmod(0o755)
            def git(*args):
                return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()
            git("init", "--quiet")
            git("add", ".")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "--quiet", "-m", "fixture")
            sha = git("rev-parse", "HEAD")
            env = os.environ.copy()
            env.update({"SOURCE_SHA": sha, "IDF_PATH": str(idf), "RUNNER_TEMP": str(root),
                        "FAKE_SIZE_REACHED": str(root / "size-reached"),
                        "GIT_CONFIG_GLOBAL": str(root / "isolated-gitconfig")})
            # Convert only path arguments for Git Bash; no shell interpolation.
            if os.name == "nt":
                for key in ("IDF_PATH", "RUNNER_TEMP", "FAKE_SIZE_REACHED", "GIT_CONFIG_GLOBAL"):
                    env[key] = subprocess.check_output([bash, "-c", 'cygpath -u "$1"', "fixture", env[key]], text=True).strip()
                out_arg = subprocess.check_output([bash, "-c", 'cygpath -u "$1"', "fixture", str(output)], text=True).strip()
            else:
                out_arg = str(output)
            result = subprocess.run([bash, "tools/build_twice.sh", out_arg], cwd=source,
                                    env=env, text=True, capture_output=True)
            self.assertEqual(1, result.returncode, result.stdout + result.stderr)
            records = [json.loads((output / label / "spy.json").read_text()) for label in ("A", "B")]
            self.assertEqual([7, 7], [r["build_exit"] for r in records])
            self.assertNotEqual(records[0]["workspace"], records[1]["workspace"])
            self.assertFalse((root / "size-reached").exists(), "size must not mask a failed build")
            for label in ("A", "B"):
                self.assertIn("CLEAN_START=1", (output / label / "clean-start.txt").read_text())
                self.assertIn("injected build failure", (output / label / "build.log").read_text())


if __name__ == "__main__":
    unittest.main()
