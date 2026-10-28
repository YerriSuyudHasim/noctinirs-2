#!/usr/bin/env python3
"""Supplement inner-layer evidence with actual composition/adapter dependencies."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shlex
import subprocess

from layer_contract import load_manifest, platform_paths
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    dest = args.evidence.resolve()
    dest.mkdir(parents=True, exist_ok=False)
    entries = json.loads((args.build / 'compile_commands.json').read_text())
    platform = platform_paths(ROOT, [r['path'] for r in load_manifest()])
    roots = [ROOT / 'main', *(ROOT / path for path in platform)]
    selected = [entry for entry in entries if any(Path(entry['file']).resolve().is_relative_to(path) for path in roots)]
    if not selected:
        raise RuntimeError('No actual adapter/composition compile commands')
    records = []
    for i, entry in enumerate(selected):
        original = entry.get('arguments') or shlex.split(entry['command'])
        cmd = []
        skip = False
        for token in original:
            if skip:
                skip = False
            elif token in ('-o', '-MF', '-MT', '-MQ'):
                skip = True
            elif token not in ('-c', '-MD', '-MMD', '-MP'):
                cmd.append(token)
        cmd += ['-M', '-v']
        result = subprocess.run(cmd, cwd=entry['directory'], capture_output=True)
        (dest / f'unit-{i}.dependencies.txt').write_bytes(result.stdout)
        (dest / f'unit-{i}.search.txt').write_bytes(result.stderr)
        if result.returncode or not result.stdout.strip():
            raise RuntimeError('Adapter dependency extraction failed: ' + entry['file'])
        records.append(dict(entry, dependency_command=cmd, exit=result.returncode))
    report = {
        'source_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'environment': {k: os.environ.get(k) for k in ('CPATH', 'C_INCLUDE_PATH', 'INCLUDE', 'SDKROOT')},
        'records': records,
    }
    (dest / 'coverage.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS supplemental actual adapter/composition dependencies: {len(records)} units')


if __name__ == '__main__':
    main()
