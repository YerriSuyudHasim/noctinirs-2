"""Execute the checked-in detector and invariant, not a reimplementation."""
import itertools
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.bash = shutil.which('bash')
        if os.name == 'nt':
            self.bash = 'C:/Program Files/Git/bin/bash.exe'
        shutil.copytree(ROOT / 'tools', self.root / 'tools',
                        ignore=shutil.ignore_patterns('__pycache__'))
        # The copied production platform manifest requires its declared directories.
        import json
        for path in json.loads((self.root / 'tools/platform_components.json').read_text())['paths']:
            (self.root / path).mkdir(parents=True, exist_ok=True)


    def block(self, name, end=None):
        source = (ROOT / '.github/workflows/firmware-build.yml').read_text()
        source = source.split('      - name: ' + name, 1)[1].split('        run: |\n', 1)[1]
        if end:
            source = source.split(end, 1)[0]
        return textwrap.dedent(source)

    def run_block(self, script, **values):
        env = dict(os.environ, GITHUB_OUTPUT=str(self.root / 'output'),
                   GITHUB_STEP_SUMMARY=str(self.root / 'summary'), **values)
        # Native Windows Python needs path conversion for Git Bash temp paths.
        if os.name == 'nt':
            script = 'python3() { "' + Path(os.sys.executable).as_posix() + '" "$@"; };\n' + script
        return subprocess.run([self.bash, '-c', script], cwd=self.root,
                              env=env, capture_output=True, text=True)

    def test_actual_detector(self):
        script = self.block('Detect layered components', '\n  static-dependency-gate:')
        for case in ('absent', 'empty', 'unknown', 'malformed', 'missing'):
            with self.subTest(case=case):
                output = self.root / 'output'
                output.unlink(missing_ok=True)
                if case == 'empty':
                    (self.root / 'components/nn2_core').mkdir(parents=True)
                elif case == 'unknown':
                    (self.root / 'components/nn2_unknown').write_text('')
                elif case == 'malformed':
                    (self.root / 'components/nn2_unknown').unlink()
                    (self.root / 'tools/layers.json').write_text('{')
                elif case == 'missing':
                    (self.root / 'tools/layers.json').unlink()
                result = self.run_block(script)
                self.assertEqual(result.returncode, 0 if case in ('absent', 'empty') else 1,
                                 result.stdout + result.stderr)
                if result.returncode:
                    self.assertFalse(output.exists(), 'failed detector emitted outputs')
                else:
                    self.assertIn('present=' + ('false' if case == 'absent' else 'true'), output.read_text())

    def test_actual_invariant_all_states(self):
        script = self.block('Check Class S gate invariant')
        for detect, present, gate in itertools.product(
                ('success', 'failure', 'cancelled', 'skipped'),
                ('true', 'false', ''), ('success', 'failure', 'cancelled', 'skipped')):
            with self.subTest(detect=detect, present=present, gate=gate):
                expected = detect == 'success' and (present, gate) in (
                    ('true', 'success'), ('false', 'skipped'))
                result = self.run_block(script, DETECT_RESULT=detect, PRESENT=present, GATE_RESULT=gate)
                self.assertEqual(result.returncode, 0 if expected else 1, result.stdout + result.stderr)
