#!/usr/bin/env python3
"""Assert reviewed-source failure, production-seam success and positive controls.

Only the legacy harness is extracted: dispatch/call/ports are verbatim; the
owner loop is wrapped after its wait, receiving the captured count as argument.
The corrected tests include exactly the internal implementation used by ESP.
No downloads, hardware or changes to reviewer evidence. Requires native compiler.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OLD = '99ffe66e51f95a17bf62194d2b7216390c08aa33'
HASH = '063dad59eb1c439d7f592a6aa431e3e27f12f1a8ea273412287d1bedd57cf7c2'


def legacy():
    raw = subprocess.check_output(['git', 'show', OLD + ':main/adapters/nn2_ads131_esp.c'], cwd=ROOT)
    if hashlib.sha256(raw).hexdigest() != HASH:
        raise RuntimeError('Reviewed control source identity mismatch')
    source = raw.decode()

    def function(signature):
        if source.count(signature) != 1:
            raise RuntimeError('Legacy signature drift: ' + signature)
        start = source.index(signature)
        end = source.index('{', start) + 1
        level = 1
        while level:
            level += (source[end] == '{') - (source[end] == '}')
            end += 1
        return source[start:end]

    owner = function('static void owner(')
    begin = '        bool discard_events = false;'
    if owner.count(begin) != 1:
        raise RuntimeError('Legacy owner seam drift')
    body = owner[owner.index(begin):owner.rindex('    }')]
    ports_start = source.index('static nn2_status_t init_port(')
    ports_end = source.index('nn2_status_t nn2_adapter_ads131_create(')
    return (function('static bool dispatch(') + '\n'
            + 'static void owner_iteration(device_t *d, uint32_t got) {\n' + body + '}\n'
            + function('static nn2_status_t call(') + '\n' + source[ports_start:ports_end])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--msvc', action='store_true')
    args = parser.parse_args()
    dest = args.evidence.resolve()
    dest.mkdir(parents=True, exist_ok=False)
    old_inc = dest / 'reviewed-control.inc'
    old_inc.write_text(legacy(), encoding='utf-8')
    records = []

    def run(cmd, name, env=None):
        result = subprocess.run(cmd, cwd=dest, env=env, capture_output=True, text=True)
        (dest / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result

    for kind in ('old', 'new'):
        exe = dest / (kind + ('.exe' if os.name == 'nt' else ''))
        defines = ['NN2_CONTROL_STANDALONE']
        if kind == 'old':
            defines += ['NN2_CONTROL_LEGACY', 'NN2_CONTROL_IMPLEMENTATION="' + old_inc.as_posix() + '"']
        includes = [ROOT / p for p in ('test/host', 'test/host/unity', 'main/adapters', 'components/nn2_core/include')]
        sources = [ROOT / p for p in ('test/host/test_ads131_control.c', 'main/adapters/nn2_ads131_logic.c', 'test/host/unity/unity.c')]
        if args.msvc:
            cmd = [args.cc, '/nologo', '/std:c11', '/W3', '/WX', '/Od']
            cmd += ['/D' + d for d in defines] + ['/I' + str(i) for i in includes]
            cmd += [str(s) for s in sources] + ['/Fe:' + str(exe)]
        else:
            cmd = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O0']
            cmd += ['-D' + d for d in defines] + ['-I' + str(i) for i in includes]
            cmd += [str(s) for s in sources] + ['-o', str(exe)]
        build = run(cmd, kind + '-build')
        (dest / (kind + '-command.json')).write_text(json.dumps(cmd, indent=2) + '\n')
        if build.returncode:
            raise RuntimeError(kind + ' build failed; see retained log')
        groups = [('R1', 4), ('R2', 7), ('positive', 10), ('zero', 0)]
        if kind == 'new':
            groups.append(('all', 22))
        for group, count in groups:
            env = dict(os.environ, NN2_CONTROL_GROUP=group)
            if group == 'all':
                env.pop('NN2_CONTROL_GROUP', None)
            result = run([str(exe)], kind + '-' + group, env)
            counts = re.findall(r'^(\d+) Tests (\d+) Failures (\d+) Ignored\s*$', result.stdout, re.M)
            expected_failures = count if kind == 'old' and group in ('R1', 'R2') else 0
            expected_exit = 1 if expected_failures or count == 0 else 0
            good = counts == [(str(count), str(expected_failures), '0')] and result.returncode == expected_exit
            record = dict(source=kind, group=group, counts=counts, exit=result.returncode, expected_exit=expected_exit, verified=good)
            records.append(record)
            print(json.dumps(record))
            if not good:
                raise RuntimeError('Unexpected regression/control result: ' + kind + '/' + group)
    report = dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  source_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  reviewed_sha=OLD, reviewed_esp_sha256=HASH,
                  legacy_extraction_sha256=hashlib.sha256(old_inc.read_bytes()).hexdigest(),
                  production_control_sha256=hashlib.sha256((ROOT / 'main/adapters/nn2_ads131_control.inc').read_bytes()).hexdigest(),
                  method=__doc__, results=records)
    (dest / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
