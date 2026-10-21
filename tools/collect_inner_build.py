#!/usr/bin/env python3
"""Retain actual product compile commands and full GCC/Clang -M/-v evidence.

This complements Class S; it does not certify unbuilt branches or link callbacks.
Run only on a completed host or IDF product build, never synthetic fixtures.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--mode', choices=('host', 'idf'), required=True)
    args = p.parse_args()
    dest = args.evidence.resolve()
    dest.mkdir(parents=True, exist_ok=False)
    commands = json.loads((args.build / 'compile_commands.json').read_text())
    shutil.copy2(args.build / 'compile_commands.json', dest / 'compile_commands.json')
    inventory = args.build / ('host_headers.tsv' if args.mode == 'host' else 'nn2_c11_header_probes.tsv')
    shutil.copy2(inventory, dest / inventory.name)
    rows = [line.split('\t') for line in inventory.read_text().splitlines()[1:]]
    header_col, probe_col = (0, 1) if args.mode == 'host' else (1, 2)
    headers = sorted(str(f.relative_to(ROOT)) for f in (ROOT / 'components').rglob('*.h'))
    if sorted(r[header_col] for r in rows) != headers:
        raise RuntimeError('Independent product header coverage mismatch')
    expected = {f.resolve() for f in (ROOT / 'components').rglob('*.c')}
    expected.update(Path(r[probe_col]).resolve() for r in rows)
    if args.mode == 'host':
        expected.update((args.build / ('combined_' + order + '.c')).resolve() for order in ('forward', 'reverse'))
    selected = [c for c in commands if Path(c['file']).resolve() in expected]
    if len(selected) != len(expected) or {Path(c['file']).resolve() for c in selected} != expected:
        raise RuntimeError('Product source/probe compile-command coverage mismatch')
    records = []
    for i, item in enumerate(selected):
        original = item.get('arguments') or shlex.split(item['command'])
        dialects = [f for f in original if f.startswith('-std=')]
        if not dialects or dialects[-1] != '-std=c11':
            raise RuntimeError('Effective dialect is not C11: ' + item['file'])
        cmd = []
        skip = False
        for token in original:
            if skip:
                skip = False
            elif token in ('-o', '-MF', '-MT', '-MQ'):
                skip = True
            elif token not in ('-c', '-MD', '-MMD', '-MP'):
                cmd.append(token)
        cmd.extend(['-M', '-v'])  # Includes system headers; -MM is insufficient.
        result = subprocess.run(cmd, cwd=item['directory'], capture_output=True)
        prefix = dest / ('unit-' + str(i))
        prefix.with_suffix('.dependencies.txt').write_bytes(result.stdout)
        prefix.with_suffix('.search.txt').write_bytes(result.stderr)
        if result.returncode or not result.stdout.strip():
            raise RuntimeError('Full dependency extraction failed: ' + item['file'])
        version = subprocess.check_output([original[0], '--version'], cwd=item['directory'], text=True)
        records.append({'source': item['file'], 'cwd': item['directory'], 'command': original,
                        'dependency_command': cmd, 'compiler_version': version, 'exit': result.returncode})
        if Path(item['file']).resolve() not in {f.resolve() for f in (ROOT / 'components').rglob('*.c')}:
            shutil.copy2(item['file'], dest / (str(i) + '-probe.c'))
    report = {'source_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'mode': args.mode,
              'environment': {k: os.environ.get(k) for k in ('CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','INCLUDE','SDKROOT')},
              'source_count': len(list((ROOT / 'components').rglob('*.c'))), 'header_count': len(headers),
              'compiled_units': len(records), 'records': records}
    if args.mode == 'idf':
        # Owner decision 6306981230 is bounded to these unmodified shim bytes.
        # Preserve the evidence; never hide it or modify SDK search roots.
        shim = Path(os.environ['IDF_PATH']) / 'components/esp_libc/platform_include/sys/cdefs.h'
        digest = hashlib.sha256(shim.read_bytes()).hexdigest()
        if digest != '45151a90d09f8e7d6b8acab050f2e15686e7fdca70d29b298df88af16d4d884a':
            raise RuntimeError('Pinned A2 libc shim changed; fresh assessment required')
        shutil.copy2(shim, dest / 'pinned-libc-cdefs.h')
        report['a2_shim'] = {'path': str(shim), 'sha256': digest,
            'owner_decision': 'https://github.com/YerriSuyudHasim/noctinirs-2/pull/15#issuecomment-6306981230'}
    (dest / 'coverage.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"PASS product {args.mode}: {report['source_count']} sources, {len(headers)} independent headers, {len(records)} commands")


if __name__ == '__main__':
    main()
