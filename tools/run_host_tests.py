#!/usr/bin/env python3
"""Execute production Class U, reject failures/zero/ignored cases, retain evidence.

Failure controls use isolated copies and never modify the product suite.
Requires a pre-existing native GCC/Clang and CMake; installs nothing.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command, cwd, log):
    with log.open('w', encoding='utf-8') as out:
        out.write('cwd=' + str(cwd) + '\ncommand=' + json.dumps(command) + '\n')
        out.flush()
        result = subprocess.run(command, cwd=cwd, stdout=out, stderr=subprocess.STDOUT)
    print(log.read_text(encoding='utf-8'), flush=True)
    return result.returncode


def execute(root, build, evidence):
    evidence.mkdir(parents=True, exist_ok=True)
    if build.exists():
        raise RuntimeError('Use a new clean build location: ' + str(build))
    if run(['cmake', '-S', str(root / 'test/host'), '-B', str(build), '-G', 'Ninja'], root,
           evidence / 'configure.log'):
        raise RuntimeError('Host configure failed')
    if run(['cmake', '--build', str(build), '--verbose'], root, evidence / 'build.log'):
        raise RuntimeError('Host build failed')
    exe = build / ('nn2_host_tests.exe' if os.name == 'nt' else 'nn2_host_tests')
    rc = run([str(exe)], root, evidence / 'tests.log')
    text = (evidence / 'tests.log').read_text()
    counts = re.findall(r'^(\d+) Tests (\d+) Failures (\d+) Ignored\s*$', text, re.M)
    valid = len(counts) == 1 and int(counts[0][0]) > 0 and counts[0][1:] == ('0', '0')
    result = {'process_exit': rc, 'counts': counts,
              'result': 'PASS' if rc == 0 and valid else 'FAIL'}
    (evidence / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--failure-controls', action='store_true')
    args = parser.parse_args()
    build, evidence = args.build.resolve(), args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    metadata = {'source_sha': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'platform': sys.platform, 'python': sys.version, 'cwd': str(ROOT),
                'include_environment': {k: os.environ.get(k) for k in
                    ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'INCLUDE', 'SDKROOT', 'CC', 'CFLAGS')}}
    (evidence / 'environment.json').write_text(json.dumps(metadata, indent=2) + '\n')
    provenance = json.loads((ROOT / 'test/host/unity/PROVENANCE.json').read_text())
    for name, entry in provenance['files'].items():
        if hashlib.sha256((ROOT / 'test/host/unity' / name).read_bytes()).hexdigest() != entry['sha256']:
            raise RuntimeError('Pinned Unity bytes differ: ' + name)
    shutil.copy2(ROOT / 'test/host/unity/PROVENANCE.json', evidence / 'unity-provenance.json')
    shutil.copy2(ROOT / 'test/host/unity/LICENSE.txt', evidence / 'unity-LICENSE.txt')
    result = execute(ROOT, build, evidence)
    if result['result'] != 'PASS':
        return 1
    if args.failure_controls:
        controls = {}
        for kind in ('assertion-failure', 'zero-tests'):
            # Retained isolated source copy is part of the control evidence.
            isolated = evidence / kind / 'source'
            for path in ('test/host', 'components', 'cmake'):
                shutil.copytree(ROOT / path, isolated / path)
            if kind == 'assertion-failure':
                path = isolated / 'test/host/test_ring.c'
                source = path.read_text()
                old = 'nn2_core_ring_init(&r) == NN2_OK'
                if old not in source:
                    raise RuntimeError('Failure control anchor missing')
                path.write_text(source.replace(old, 'nn2_core_ring_init(&r) == NN2_ERR_IO', 1))
            else:
                path = isolated / 'test/host/test_main.c'
                source = path.read_text()
                for suite in ('ring', 'sequencer', 'contract'):
                    source = source.replace('    test_' + suite + '_suite();', '')
                path.write_text(source)
            control = execute(isolated, evidence / kind / 'build', evidence / kind)
            controls[kind] = control
            if control['result'] != 'FAIL' or control['process_exit'] == 0:
                raise RuntimeError('Failure did not propagate: ' + kind)
        (evidence / 'failure-propagation.json').write_text(json.dumps(controls, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print('FAIL:', error, file=sys.stderr)
        sys.exit(1)
