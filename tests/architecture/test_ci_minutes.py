"""Static contracts for the Issue 59 Actions-minute workflow changes."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / '.github' / 'workflows'
FIRMWARE = (WORKFLOWS / 'firmware-build.yml').read_text()
REFERENCE = (WORKFLOWS / 'reference-integrity.yml').read_text()
REPRO = (WORKFLOWS / 'reproducible-build.yml').read_text()
IMAGE = 'espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c'
HEAD_REF = 'ref: ${{ github.event.pull_request.head.sha || github.sha }}'


def trigger_block(workflow):
    return workflow.split('\non:', 1)[1].split('\npermissions:', 1)[0]


class DocsOnlyClassifierTests(unittest.TestCase):
    def test_host_allowlist_script(self):
        script = ROOT / '.github' / 'actions' / 'detect-docs-only' / 'detect_test.sh'
        result = subprocess.run(['bash', str(script)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('detect_test: PASS', result.stdout)


class WorkflowMinutePolicyTests(unittest.TestCase):
    def test_one_run_set_and_no_path_filters(self):
        for name, workflow in (
                ('firmware-build.yml', FIRMWARE),
                ('reference-integrity.yml', REFERENCE),
                ('reproducible-build.yml', REPRO)):
            with self.subTest(workflow=name):
                triggers = trigger_block(workflow)
                self.assertIn('\n  push:\n    branches:\n      - main\n', triggers)
                self.assertIn('\n  pull_request:', triggers)
                self.assertNotIn('paths:', triggers)
                self.assertNotIn('paths-ignore:', triggers)
                self.assertNotIn('pull_request_target:', workflow)
                self.assertNotIn('continue-on-error', workflow)
                self.assertNotIn('secrets.', workflow)
                self.assertIn('\npermissions:\n  contents: read\n', workflow)
                self.assertIn('group: ${{ github.workflow }}-${{ github.ref }}', workflow)
                self.assertIn(
                    "cancel-in-progress: ${{ github.event_name == 'pull_request' && github.ref != 'refs/heads/main' }}",
                    workflow)
                self.assertIn('persist-credentials: false', workflow)

    def test_preexisting_check_names_remain(self):
        for name in (
                'Layer detect (informational, not a conformance check)',
                'class-s-dependency-gate',
                'class-s-gate-selftest',
                'architecture-contract',
                'class-b-esp-idf-build${{ matrix.suffix }}',
                'class-u-host-tests',
                'class-s-gate-invariant'):
            self.assertIn(f'name: {name}\n', FIRMWARE)
        self.assertIn('name: reference-integrity\n', REFERENCE)
        self.assertIn('name: reproducibility-comparison-tests\n', REPRO)
        self.assertIn('name: class-b-reproducible-build${{ matrix.suffix }}\n', REPRO)
        self.assertEqual(FIRMWARE.count('name: detect-changes\n'), 1)
        self.assertEqual(REPRO.count('name: detect-changes\n'), 1)
        self.assertNotIn('detect-changes', REFERENCE)

    def test_docs_skip_is_explicit_and_narrow(self):
        self.assertIn('echo "SKIPPED: docs-only (files: ${CHANGED_FILES:-})"', FIRMWARE)
        self.assertIn('echo "SKIPPED: docs-only (files: ${CHANGED_FILES:-})"', REPRO)
        self.assertNotIn('SKIPPED:', REFERENCE)
        self.assertNotIn('docs_only', REFERENCE)
        # Always-on gates do not consult the docs-only flag.
        for name in ('class-s-dependency-gate', 'class-s-gate-selftest', 'class-u-host-tests', 'class-s-gate-invariant'):
            job = FIRMWARE.split(f'name: {name}\n', 1)[1].split('\n  ', 1)[0]
            self.assertNotIn('docs_only', job)
            self.assertNotIn('SKIPPED:', job)

    def test_reproducible_draft_skip_and_clean_environment(self):
        self.assertIn('ready_for_review', trigger_block(REPRO))
        self.assertIn(
            'echo "SKIPPED: draft pull request (reproducible build runs on ready_for_review, non-draft synchronize, and main)"',
            REPRO)
        self.assertNotIn('draft pull request', FIRMWARE)
        self.assertNotIn('actions/cache', REPRO)
        self.assertNotIn('CCACHE', REPRO)
        self.assertNotIn('restore-keys', REPRO)
        self.assertEqual(REPRO.count(IMAGE), 2)
        self.assertEqual(REPRO.count(HEAD_REF), 2)
        self.assertIn('if: always()', REPRO)
        self.assertIn(
            "if: ${{ !cancelled() && needs.comparison-tests.result == 'success' }}",
            REPRO)
        self.assertIn('if: ${{ !cancelled() }}', FIRMWARE)
        self.assertIn('include-hidden-files: true', REPRO)
        self.assertIn('retention-days: 90', REPRO)

    def test_ordinary_build_cache_cannot_restore_into_main(self):
        self.assertIn(IMAGE, FIRMWARE)
        self.assertEqual(FIRMWARE.count('actions/cache@'), 1)
        self.assertNotIn('restore-keys', FIRMWARE)
        self.assertNotIn('fail-on-cache-miss: true', FIRMWARE)
        cache = FIRMWARE.split('actions/cache@', 1)[1].split('\n      - name:', 1)[0]
        self.assertIn('scope-main', cache)
        self.assertIn('scope-pr', cache)
        self.assertIn('env.EXPECTED_IDF_COMMIT', cache)
        self.assertIn('hashFiles(', cache)
        self.assertIn('sdkconfig.defaults', cache)
        self.assertIn('**/dependencies.lock', cache)
        self.assertIn("github.ref == 'refs/heads/main'", cache)
        self.assertIn('CCACHE_BASEDIR', FIRMWARE)

    def test_reference_integrity_stays_on_windows(self):
        self.assertIn('runs-on: windows-latest\n', REFERENCE)
        self.assertNotIn('ubuntu', REFERENCE)


if __name__ == '__main__':
    unittest.main()
