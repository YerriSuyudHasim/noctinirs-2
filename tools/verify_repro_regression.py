#!/usr/bin/env python3
"""Prove PR #40 regressions reject the reviewed implementation, without edits."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REVIEWED = 'f97767035e0ab4f801e655ec751141314668c407'


def main():
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        for name in ('tools/reproducible_build.py', '.github/workflows/firmware-build.yml'):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(subprocess.check_output(['git', 'show', f'{REVIEWED}:{name}'], cwd=ROOT))
        tests = root / 'tests/reproducibility'
        tests.mkdir(parents=True)
        for name in ('test_comparison.py', 'test_fail_closed.py'):
            shutil.copyfile(ROOT / 'tests/reproducibility' / name, tests / name)
        result = subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', str(tests), '-p', 'test_fail_closed.py', '-v'], cwd=root, capture_output=True, text=True)
        print(f'Reviewed source: {REVIEWED}\n{result.stdout}{result.stderr}')
        expected = ('test_nested_enumeration_failure_never_reports_matching_inventory',
                    'test_nested_enumeration_failure_records_incomplete_snapshot',
                    'test_exact_hash_outputs_block_fails_closed')
        if result.returncode != 1 or any(f'FAIL: {name}' not in result.stderr for name in expected):
            print('FAIL: baseline did not reproduce both findings', file=sys.stderr)
            return 1
        print('PASS: regression-first controls reproduced F1 capture/compare and F2 workflow failures.')
        return 0


if __name__ == '__main__':
    sys.exit(main())
