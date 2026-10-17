#!/usr/bin/env python3
"""Compile synthetic layers through actual ESP-IDF registration and build."""
from pathlib import Path
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from layer_contract import load_manifest
repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='nn2-c11-idf-') as tmp:
    root=Path(tmp);shutil.copytree(repo/'cmake',root/'cmake');(root/'tools').mkdir()
    for name in ('layer_contract.py','layers.json'):shutil.copy2(repo/'tools'/name,root/'tools'/name)
    (root/'CMakeLists.txt').write_text("""cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nn2_contract_fixture)
include(cmake/nn2_c11.cmake)
nn2_enforce_idf_layers("${CMAKE_CURRENT_SOURCE_DIR}")
""")
    (root/'main').mkdir()
    deps=' '.join(Path(row['path']).name for row in load_manifest())
    (root/'main/CMakeLists.txt').write_text('idf_component_register(SRCS "main.c" REQUIRES '+deps+')\n')
    (root/'main/main.c').write_text('void app_main(void) {}\n')
    for row in load_manifest():
        d=root/row['path'];d.mkdir(parents=True)
        if row['name']=='ports':
            (d/'port.h').write_text('typedef struct { void *ctx; int (*read)(void *ctx); } nn2_port_fixture_t;\n')
            (d/'CMakeLists.txt').write_text('idf_component_register(INCLUDE_DIRS ".")\n')
        else:
            (d/'fixture.c').write_text('_Static_assert(__STDC_VERSION__ == 201112L, "C11");\nint nn2_'+row['name']+'_fixture(void) { return _Generic(1, int: 1); }\n')
            (d/'CMakeLists.txt').write_text('idf_component_register(SRCS "fixture.c")\n')
    idf=Path(os.environ['IDF_PATH'])/'tools/idf.py'
    for action in ('set-target','build'):
        cmd=[sys.executable,str(idf),'-C',str(root),action]
        if action=='set-target':cmd.append('esp32s3')
        subprocess.run(cmd,check=True)
    commands=json.loads((root/'build/compile_commands.json').read_text());count=0
    for entry in commands:
        file=Path(entry['file']).resolve()
        if any((root/row['path']).resolve() in file.parents for row in load_manifest()) or file.name.startswith('nn2_c11_probe_'):
            flags=[arg for arg in shlex.split(entry['command']) if arg.startswith('-std=')]
            if not flags or flags[-1]!='-std=c11':raise SystemExit('FAIL: effective dialect '+str(flags))
            count+=1
    if count!=5:raise SystemExit('FAIL: expected 2 source and 3 probe commands, got '+str(count))
    print('PASS: actual ESP-IDF C11 contract fixture: 5 compiled commands (2 sources, 3 header probes)')
