#!/usr/bin/env python3
"""Compile synthetic layers through actual ESP-IDF; retain auditable coverage."""
from pathlib import Path
import csv
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from layer_contract import load_manifest

repo = Path(__file__).resolve().parents[1]
evidence = Path(os.environ.get('NN2_IDF_EVIDENCE_DIR', 'evidence/c11-idf')).resolve()
evidence.mkdir(parents=True, exist_ok=False)
with tempfile.TemporaryDirectory(prefix='nn2-c11-idf-') as tmp:
    root = Path(tmp)
    try:
        shutil.copytree(repo/'cmake', root/'cmake')
        (root/'tools').mkdir()
        for name in ('layer_contract.py', 'layers.json'):
            shutil.copy2(repo/'tools'/name, root/'tools'/name)
        (root/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nn2_contract_fixture)
include(cmake/nn2_c11.cmake)
nn2_enforce_idf_layers("${CMAKE_CURRENT_SOURCE_DIR}")
''')
        (root/'main').mkdir()
        rows = load_manifest()
        deps = ' '.join(Path(row['path']).name for row in rows)
        (root/'main/CMakeLists.txt').write_text('idf_component_register(SRCS "main.c" REQUIRES '+deps+')\n')
        (root/'main/main.c').write_text('void app_main(void) {}\n')
        expected_sources = set()
        expected_headers = set()
        for row in rows:
            d = root/row['path']
            d.mkdir(parents=True)
            if row['name'] == 'ports':
                # Header-only component, with two same-basename, same-guard headers.
                for folder in ('a', 'b'):
                    (d/folder).mkdir()
                    (d/folder/'port.h').write_text('#ifndef PORT_H\n#define PORT_H\ntypedef struct { void *ctx; int (*read)(void *ctx); } nn2_port_fixture_t;\n#endif\n')
                (d/'CMakeLists.txt').write_text('idf_component_register(INCLUDE_DIRS ".")\n')
            else:
                (d/'context').mkdir()
                (d/'context/context.h').write_text('#define NN2_PRIVATE_CONTEXT 1\n')
                for folder, member in (('a', 'int'), ('b', 'double')):
                    (d/folder).mkdir()
                    (d/folder/'private.h').write_text('#ifndef '+folder.upper()+'_PRIVATE_H\n#define '+folder.upper()+'_PRIVATE_H\n#include "context.h"\n_Static_assert(NN2_PRIVATE_CONTEXT == 1, "context");\ntypedef struct { '+member+' x; } private_state_t;\n#endif\n')
                    source = d/(folder+'.c')
                    source.write_text('#include "'+folder+'/private.h"\n_Static_assert(__STDC_VERSION__ == 201112L, "C11");\nint nn2_'+row['name']+'_'+folder+'(void) { return _Generic(1, int: sizeof(private_state_t)); }\n')
                    expected_sources.add(source.resolve())
                (d/'CMakeLists.txt').write_text('idf_component_register(SRCS "a.c" "b.c" PRIV_INCLUDE_DIRS "context")\n')
            expected_headers.update(h.relative_to(root).as_posix() for h in d.rglob('*.h'))
        idf = Path(os.environ['IDF_PATH'])/'tools/idf.py'
        for action in ('set-target', 'build'):
            cmd = [sys.executable, str(idf), '-C', str(root), action]
            if action == 'set-target':
                cmd.append('esp32s3')
            with (evidence/(action+'.log')).open('w', encoding='utf-8') as log:
                log.write(json.dumps(cmd)+'\n'); log.flush()
                p = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
            if p.returncode:
                raise SystemExit('FAIL: IDF '+action+'; see '+str(evidence/(action+'.log')))
        with (root/'build/nn2_c11_header_probes.tsv').open() as f:
            inventory = list(csv.DictReader(f, delimiter='\t'))
        if len(inventory) != len(expected_headers) or {r['header'] for r in inventory} != expected_headers:
            raise SystemExit('FAIL: header inventory does not cover every fixture header exactly once')
        probes = {Path(r['probe']).resolve() for r in inventory}
        if len(probes) != len(expected_headers) or len({r['target'] for r in inventory}) != len(expected_headers):
            raise SystemExit('FAIL: colliding probe paths or targets')
        for row in inventory:
            text = Path(row['probe']).read_text()
            if text.count('#include') != 1 or '#include "'+(root/row['header']).as_posix()+'"' not in text:
                raise SystemExit('FAIL: probe is not an independent include of its inventoried header')
        expected = expected_sources | probes
        commands = json.loads((root/'build/compile_commands.json').read_text())
        covered = []
        for entry in commands:
            file = Path(entry['file']).resolve()
            if file in expected:
                flags = [arg for arg in shlex.split(entry['command']) if arg.startswith('-std=')]
                if not flags or flags[-1] != '-std=c11':
                    raise SystemExit('FAIL: effective dialect '+str(flags))
                covered.append(file)
        if set(covered) != expected or len(covered) != len(expected):
            raise SystemExit('FAIL: expected source/header coverage differs from compile commands')
        (evidence/'coverage.json').write_text(json.dumps({'fixture_root':str(root), 'sources':sorted(map(str,expected_sources)), 'headers':sorted(expected_headers), 'probes':inventory, 'compiled':sorted(map(str,covered))}, indent=2))
        print(f'PASS: actual ESP-IDF C11 contract fixture: {len(expected_sources)} sources, {len(expected_headers)} independent header probes, {len(covered)} commands')
    finally:
        # Keep generated probes, source context, inventory and exact compiler commands
        # even after failure; build objects/binaries are not required for this fixture.
        for folder in ('cmake', 'tools', 'main', 'components'):
            if (root/folder).exists():
                shutil.copytree(root/folder, evidence/folder, ignore=shutil.ignore_patterns('__pycache__'))
        if (root/'CMakeLists.txt').exists():
            shutil.copy2(root/'CMakeLists.txt', evidence/'CMakeLists.txt')
        if (root/'build').exists():
            for file in (root/'build').iterdir():
                if file.name.startswith('nn2_c11_') or file.name == 'compile_commands.json':
                    shutil.copy2(file, evidence/file.name)
