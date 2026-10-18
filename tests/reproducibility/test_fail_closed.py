"""PR #40 F1/F2: real binary/log fixtures with targeted OS failure injection."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import test_comparison as fixtures
from test_comparison import ROOT, repro


class TraversalComparisonTests(unittest.TestCase):
    setUp = fixtures.ComparisonTests.setUp
    manifest = fixtures.ComparisonTests.manifest
    compare = fixtures.ComparisonTests.compare
    def test_nested_enumeration_failure_never_reports_matching_inventory(self):
        for error_type in (PermissionError, FileNotFoundError, OSError):
            with self.subTest(error=error_type.__name__):
                hidden = self.b / 'bins/unreadable'
                hidden.mkdir(exist_ok=True)
                extra = hidden / 'extra.bin'
                extra.write_bytes(b'not in recorded manifest')
                real_scan = os.scandir
                def scan(path):
                    if Path(path) == hidden:
                        raise error_type('injected nested enumeration failure')
                    return real_scan(path)
                with patch.object(repro.os, 'scandir', side_effect=scan):
                    report = self.compare(False)
                self.assertTrue(extra.is_file())
                self.assertTrue(any('injected nested' in e for e in report['errors']))
                self.assertFalse(any(r['status'] == 'IDENTICAL' for r in report['artifacts']))

    def test_binary_read_error_is_failure_with_diagnostics(self):
        real_read = Path.read_bytes
        def read(path):
            if path == self.b / 'bins/bootloader/bootloader.bin':
                raise PermissionError('injected nested binary read failure')
            return real_read(path)
        with patch.object(Path, 'read_bytes', read):
            report = self.compare(False)
        self.assertTrue(any('nested binary read' in e for e in report['errors']))

    def test_retained_missing_or_invalid_warning_log_cannot_pass(self):
        log = self.b / 'build.log'
        for contents in (None, b'', b'\xff', b'build\x00complete'):
            with self.subTest(contents=contents):
                log.unlink(missing_ok=True)
                if contents is not None:
                    log.write_bytes(contents)
                self.compare(False)


class TraversalSnapshotTests(unittest.TestCase):
    setUp = fixtures.SnapshotTests.setUp
    snapshot = fixtures.SnapshotTests.snapshot
    def test_nested_enumeration_failure_records_incomplete_snapshot(self):
        hidden = self.work / 'build/nested/optional'
        hidden.mkdir(parents=True)
        (hidden / 'extra.bin').write_bytes(b'optional produced binary')
        real_scan = os.scandir
        for error_type in (PermissionError, FileNotFoundError):
            with self.subTest(error=error_type.__name__):
                def scan(path):
                    if Path(path) == hidden:
                        raise error_type('injected nested snapshot enumeration failure')
                    return real_scan(path)
                with patch.object(repro.os, 'scandir', side_effect=scan):
                    self.assertFalse(self.snapshot())
                data = json.loads((self.output / 'manifest.json').read_text())
                self.assertFalse(data['complete'])
                self.assertEqual({}, data['artifacts'])
                self.assertTrue(any('nested snapshot' in e for e in data['errors']))

    def test_missing_unreadable_nonregular_and_invalid_snapshot_logs_fail(self):
        log = self.output / 'build.log'
        log.unlink()
        self.assertFalse(self.snapshot())
        log.mkdir()
        self.assertFalse(self.snapshot())
        log.rmdir()
        for data in (b'', b'\xff', b'build\x00complete'):
            log.write_bytes(data)
            self.assertFalse(self.snapshot())
        log.write_text('Build complete\n')
        real_open = os.open
        def denied(path, *args, **kwargs):
            if Path(path) == log:
                raise PermissionError('injected unreadable warning log')
            return real_open(path, *args, **kwargs)
        with patch.object(repro.os, 'open', side_effect=denied):
            self.assertFalse(self.snapshot())
        self.assertNotIn('warning_count', json.loads((self.output / 'manifest.json').read_text())['metadata'])


class WarningWorkflowTests(unittest.TestCase):
    def test_exact_hash_outputs_block_fails_closed(self):
        bash = shutil.which('bash')
        if not bash:
            self.skipTest('Bash required')
        workflow = (ROOT / '.github/workflows/firmware-build.yml').read_text()
        block = workflow.split('      - name: Hash outputs\n', 1)[1].split('\n      - name:', 1)[0]
        script = '\n'.join(line[10:] for line in block.split('        run: |\n', 1)[1].splitlines())
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'tools').mkdir()
            shutil.copyfile(ROOT / 'tools/reproducible_build.py', root / 'tools/reproducible_build.py')
            for name in ('build/noctinirs2.bin', 'build/bootloader/bootloader.bin', 'build/partition_table/partition-table.bin', 'sdkconfig'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'valid fixture')
            (root / 'evidence').mkdir()
            # Windows Git Bash does not reliably resolve python3; keep the real
            # interpreter while testing the entire committed shell block.
            prefix = ''
            if os.name == 'nt':
                exe = sys.executable.replace('\\', '/')
                prefix = f'python3() {{ "{exe}" "$@"; }}\n'
            log = root / 'evidence/build.log'
            cases = [('zero', b'Build complete\n', True), ('warning', b'main.c: WARNING: example\n', False),
                     ('missing', None, False), ('directory', None, False), ('empty', b'', False),
                     ('invalid-utf8', b'\xff\n', False), ('binary', b'Build\x00complete', False)]
            if os.name == 'posix':
                cases.extend([('fifo', None, False), ('symlink', None, False)])
                if os.geteuid() != 0:
                    cases.append(('unreadable', b'Build complete\n', False))
            for name, contents, expected in cases:
                with self.subTest(case=name):
                    if log.is_dir():
                        log.rmdir()
                    else:
                        log.unlink(missing_ok=True)
                    if name == 'directory':
                        log.mkdir()
                    elif name == 'fifo':
                        os.mkfifo(log)
                    elif name == 'symlink':
                        log.symlink_to(root / 'sdkconfig')
                    elif contents is not None:
                        log.write_bytes(contents)
                    if name == 'unreadable':
                        log.chmod(0)
                    count = root / 'evidence/warning-count.txt'
                    count.write_text('0\n')  # A previous zero must not mask failure.
                    result = subprocess.run([bash, '-c', prefix + script], cwd=root, capture_output=True, text=True, timeout=20)
                    if name == 'unreadable':
                        log.chmod(0o600)
                    self.assertEqual(expected, result.returncode == 0, result.stdout + result.stderr)
                    if name == 'zero':
                        self.assertEqual('0\n', count.read_text())
                    elif name == 'warning':
                        self.assertEqual('1\n', count.read_text())
                    else:
                        self.assertFalse(count.exists(), 'invalid log must not retain a plausible zero count')
