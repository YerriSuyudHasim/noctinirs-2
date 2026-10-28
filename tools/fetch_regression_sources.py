#!/usr/bin/env python3
"""Materialize exact regression-first commits, including unadvertised pre-squash history.

No source substitution, fallback baseline or changes to pinned commits. A complete
branch checkout does not fetch every PR's pre-squash/orphaned object. Fetch by its
existing full commit ID, then verify every consumed blob by SHA256. Fail closed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def ensure(group, root=ROOT, verify_only=False):
    pins = json.loads((Path(root) / 'tools/regression_sources.json').read_text(encoding='utf-8'))[group]
    sha = pins['commit']
    if subprocess.run(['git', 'cat-file', '-e', sha + '^{commit}'], cwd=root,
                      capture_output=True).returncode:
        if verify_only:
            raise RuntimeError('Pinned commit unavailable: ' + sha)
        subprocess.run(['git', 'fetch', '--no-tags', 'origin', sha], cwd=root, check=True)
    for path, expected in pins['files'].items():
        raw = subprocess.check_output(['git', 'show', sha + ':' + path], cwd=root)
        actual = hashlib.sha256(raw).hexdigest()
        if actual != expected:
            raise RuntimeError('Pinned regression blob mismatch: ' + sha + ':' + path)
        print('PASS pinned regression blob', sha, path, actual)
    return pins


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('group', choices=['c11', 'adc', 'reproducibility'])
    p.add_argument('--verify-only', action='store_true')
    args = p.parse_args()
    ensure(args.group, verify_only=args.verify_only)
