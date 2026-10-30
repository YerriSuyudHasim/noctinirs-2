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
import shlex
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
    if run(['cmake', '-S', str(root / 'test/host'), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'], root,
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


def check_public_opacity(build, evidence):
    """Use the actual isolated product command: public fields must not compile."""
    commands = json.loads((build / 'compile_commands.json').read_text())
    entry = next(c for c in commands if Path(c['file']).name == 'nn2_core_ring.c')
    # CMake emits native backslashes in Windows command/file entries. They
    # are path separators, not POSIX shlex escapes.
    command = entry.get('command', '')
    if os.name == 'nt':
        command = command.replace('\\', '/')
    original = entry.get('arguments') or shlex.split(command)
    source = entry['file'].replace('\\', '/') if os.name == 'nt' else entry['file']
    base = []
    skip = False
    for token in original:
        if skip:
            skip = False
        elif token in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
        elif token not in ('-c', '-MD', '-MMD', '-MP', source):
            base.append(token)
    cases = {
        'public-control': ('#include "nn2_core_ring.h"\n#include "nn2_app_sequencer.h"\n'
                           'int control(nn2_core_ring_t *r, nn2_app_sequencer_t *s) { return r != 0 && s != 0; }\n', True),
        'ring-incomplete': ('#include "nn2_core_ring.h"\nint invalid(nn2_core_ring_t *r) { return (int)r->count; }\n', False),
        'seq-incomplete': ('#include "nn2_app_sequencer.h"\nint invalid(nn2_app_sequencer_t *s) { return s->cfg.channel; }\n', False),
    }
    results = {}
    for name, (source, should_compile) in cases.items():
        path = evidence / (name + '.c')
        path.write_text(source)
        rc = run(base + ['-fsyntax-only', str(path)], Path(entry['directory']), evidence / (name + '.log'))
        if (rc == 0) != should_compile:
            raise RuntimeError('Public opacity probe unexpected result: ' + name)
        if not should_compile and 'incomplete' not in (evidence / (name + '.log')).read_text().lower():
            raise RuntimeError('Opacity probe failed for an unrelated reason: ' + name)
        results[name] = {'process_exit': rc, 'expected_compile': should_compile}
    (evidence / 'public-opacity.json').write_text(json.dumps(results, indent=2) + '\n')


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
    check_public_opacity(build, evidence)
    if args.failure_controls:
        controls = {}
        for kind in ('assertion-failure', 'zero-tests'):
            # Retained isolated source copy is part of the control evidence.
            isolated = evidence / kind / 'source'
            for path in ('test/host', 'test/golden', 'components', 'cmake', 'main/adapters', 'tools',
                         'firmware/v4-idf61/components/nocti_v4_common'):
                shutil.copytree(ROOT / path, isolated / path)
            if kind == 'assertion-failure':
                path = isolated / 'test/host/test_ring.c'
                source = path.read_text()
                old = 'nn2_core_ring_init(&r_storage, &r) == NN2_OK'
                if old not in source:
                    raise RuntimeError('Failure control anchor missing')
                path.write_text(source.replace(old, 'nn2_core_ring_init(&r_storage, &r) == NN2_ERR_IO', 1))
            else:
                path = isolated / 'test/host/test_main.c'
                source = path.read_text()
                for suite in ('ring', 'sequencer', 'contract', 'storage', 'acquisition', 'ads131', 'ads131_control', 'ads131_transport', 'v4_rtos', 'v4_sample_clock', 'v4_sample_edge'):
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
