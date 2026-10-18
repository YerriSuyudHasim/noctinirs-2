#!/usr/bin/env python3
"""Preserve regression-first F1 evidence using the unchanged reviewed helper."""
import hashlib
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REVIEWED_SHA = '789b4fa8bb2904a85116c6c6116973c348565c05'
CASES = ['test_helper_separate_private_headers',
         'test_helper_matching_guards_cannot_hide_non_c11']

with tempfile.TemporaryDirectory(prefix='nn2-reviewed-helper-') as tmp:
    module = Path(tmp)
    for name in ('nn2_c11.cmake', 'nn2_c11_guard.h'):
        data = subprocess.check_output(['git', 'show', REVIEWED_SHA+':cmake/'+name], cwd=ROOT)
        (module/name).write_bytes(data)
        print(REVIEWED_SHA, name, 'SHA256='+hashlib.sha256(data).hexdigest(), flush=True)
    if os.environ.get('NN2_C11_EVIDENCE_DIR'):
        shutil.copytree(module, Path(os.environ['NN2_C11_EVIDENCE_DIR'])/'reviewed-module')
    os.environ['NN2_TEST_C11_MODULE'] = str(module/'nn2_c11.cmake')
    sys.path.insert(0, str(ROOT/'tests/architecture'))
    import test_c11
    suite = unittest.TestSuite(test_c11.C11Tests(name) for name in CASES)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    failed = {test._testMethodName for test, _ in result.failures}
    markers = {'test_helper_separate_private_headers': 'F1: valid separate private headers rejected',
               'test_helper_matching_guards_cannot_hide_non_c11': 'F1: non-C11 header hidden by matching guard'}
    expected_assertions = all(markers[test._testMethodName] in trace
                              for test, trace in result.failures)
    if result.errors or failed != set(CASES) or result.testsRun != len(CASES) or not expected_assertions:
        raise SystemExit('FAIL: expected both F1 assertion failures and no setup errors')
    print('PASS: both new regressions expose the unchanged reviewed implementation')
