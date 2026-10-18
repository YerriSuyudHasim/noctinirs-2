"""Build contract fixtures; not product Class U or hardware evidence."""
import os
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class C11Tests(unittest.TestCase):
    def setUp(self):
        t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);self.root=Path(t.name)
        self.cc=os.environ.get('NN2_TEST_CC') or shutil.which('cc') or shutil.which('clang')
        if not self.cc:self.fail('GCC/Clang required; missing compiler is not PASS')
        for tool in ('cmake','ninja'):
            if not shutil.which(tool):self.fail(tool+' required')
        evidence=os.environ.get('NN2_C11_EVIDENCE_DIR')
        if evidence:
            self.addCleanup(lambda: shutil.copytree(self.root,Path(evidence)/self._testMethodName,
                ignore=shutil.ignore_patterns('CMakeFiles','__pycache__','*.obj','*.o','*.a','*.lib'),
                dirs_exist_ok=False))
    def run_cmake(self,*args):
        p=subprocess.run(['cmake',*args],capture_output=True,text=True)
        with (self.root/'commands.log').open('a',encoding='utf-8') as f:
            f.write(json.dumps(['cmake',*args])+'\n'+p.stdout+p.stderr+'\nexit='+str(p.returncode)+'\n')
        return p
    def build(self,source,override=''):
        (self.root/'fixture.c').write_text(source)
        module=(ROOT/'cmake/nn2_c11.cmake').as_posix()
        (self.root/'CMakeLists.txt').write_text(f"""cmake_minimum_required(VERSION 3.16)
project(c11_contract C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS ON)
add_library(inner OBJECT fixture.c)
target_compile_options(inner PRIVATE -std=gnu17)
include("{module}")
nn2_require_c11(inner)
{override}
""")
        p=self.run_cmake('-S',str(self.root),'-B',str(self.root/'build'),'-G','Ninja','-DCMAKE_C_COMPILER='+self.cc,'-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        return self.run_cmake('--build',str(self.root/'build'),'--verbose')

    def layer_fixture(self,headers,interface=False):
        """Exercise the actual helper with CMake targets standing in for IDF registration."""
        (self.root/'tools').mkdir()
        for name in ('layer_contract.py','layers.json'):
            shutil.copy2(ROOT/'tools'/name,self.root/'tools'/name)
        d=self.root/'components/nn2_ports' if interface else self.root/'components/nn2_core'
        d.mkdir(parents=True)
        for name,source in headers.items():
            h=d/name;h.parent.mkdir(parents=True,exist_ok=True);h.write_text(source)
        for stem in ('a','b'):
            (d/(stem+'.c')).write_text(f'#include "{stem}/private.h"\nint {stem}(void) {{ return sizeof(private_state_t); }}\n')
        (d/'context').mkdir()
        (d/'context/context.h').write_text('#define NN2_CONTEXT 1\n')
        module=Path(os.environ.get('NN2_TEST_C11_MODULE',ROOT/'cmake/nn2_c11.cmake')).as_posix()
        target='add_library(component INTERFACE)' if interface else 'add_library(component OBJECT components/nn2_core/a.c components/nn2_core/b.c)'
        visibility='INTERFACE' if interface else 'PRIVATE'
        (self.root/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(header_contract C)
{target}
target_include_directories(component {visibility} "{d.as_posix()}/context")
target_compile_definitions(component {visibility} NN2_COMPONENT_CONTEXT=1)
function(idf_component_get_property out component property)
    set(${{out}} component PARENT_SCOPE)
endfunction()
include("{module}")
nn2_enforce_idf_layers("${{CMAKE_CURRENT_SOURCE_DIR}}")
''')
        p=self.run_cmake('-S',str(self.root),'-B',str(self.root/'build'),'-G','Ninja','-DCMAKE_C_COMPILER='+self.cc,'-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        return d

    def test_helper_separate_private_headers(self):
        self.layer_fixture({
            'a/private.h':'#ifndef A_PRIVATE_H\n#define A_PRIVATE_H\ntypedef struct { int x; } private_state_t;\n#endif\n',
            'b/private.h':'#ifndef B_PRIVATE_H\n#define B_PRIVATE_H\ntypedef struct { double x; } private_state_t;\n#endif\n'})
        # Positive control: the unchanged helper accepts the real, separate source TUs.
        p=self.run_cmake('--build',str(self.root/'build'),'--target','component','--verbose')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        p=self.run_cmake('--build',str(self.root/'build'),'--verbose')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)

    def test_helper_matching_guards_cannot_hide_non_c11(self):
        self.layer_fixture({
            'a/private.h':'#ifndef SHARED_PRIVATE_H\n#define SHARED_PRIVATE_H\ntypedef int private_state_t;\n#endif\n',
            'b/private.h':'#ifndef SHARED_PRIVATE_H\n#define SHARED_PRIVATE_H\ntypedef int private_state_t;\n#endif\n',
            'z/private.h':'#ifndef SHARED_PRIVATE_H\n#define SHARED_PRIVATE_H\nstatic inline int bad(void) { return ({ 1; }); }\n#endif\n'})
        p=self.run_cmake('--build',str(self.root/'build'),'--target','component','--verbose')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        p=self.run_cmake('--build',str(self.root/'build'),'--verbose')
        self.assertNotEqual(p.returncode,0,'non-C11 header hidden by matching guard\n'+p.stdout+p.stderr)
        self.assertIn('z/private.h',(p.stdout+p.stderr).replace('\\','/'))

    def test_helper_header_only_ports(self):
        self.layer_fixture({'port.h':'#include "context.h"\n_Static_assert(NN2_COMPONENT_CONTEXT == NN2_CONTEXT, "context");\nstatic inline int bad(void) { return ({ 1; }); }\n'},interface=True)
        p=self.run_cmake('--build',str(self.root/'build'),'--verbose')
        self.assertNotEqual(p.returncode,0)
        self.assertIn('port.h',p.stdout+p.stderr)

    def test_helper_private_context_and_collision_safe_coverage(self):
        context='#include "context.h"\n_Static_assert(NN2_CONTEXT == NN2_COMPONENT_CONTEXT, "context");\n'
        headers={name:context+'typedef int private_state_t;\n' for name in
                 ('a/private.h','b/private.h','a-b/private.h','a_b/private.h')}
        self.layer_fixture(headers)
        p=self.run_cmake('--build',str(self.root/'build'),'--verbose')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        import csv
        with (self.root/'build/nn2_c11_header_probes.tsv').open() as f:
            inventory=list(csv.DictReader(f,delimiter='\t'))
        expected={p.relative_to(self.root).as_posix() for p in (self.root/'components').rglob('*.h')}
        self.assertEqual({row['header'] for row in inventory},expected)
        self.assertEqual(len(inventory),len(expected))
        self.assertEqual(len({row['target'] for row in inventory}),len(expected))
        commands=json.loads((self.root/'build/compile_commands.json').read_text())
        for row in inventory:
            source=Path(row['probe'])
            self.assertEqual(source.read_text().count('#include'),1)
            self.assertEqual(sum(Path(c['file']).resolve()==source.resolve() for c in commands),1)
    def test_c11_positive_effective_flags(self):
        p=self.build('_Static_assert(__STDC_VERSION__ == 201112L, "C11");\nint fixture(void) { return _Generic(1, int: 1); }\n')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertIn('-std=c11',p.stdout)
    def test_later_c17_override_fails(self):
        p=self.build('int fixture(void) {return 0;}\n','set_source_files_properties(fixture.c PROPERTIES COMPILE_OPTIONS -std=c17)')
        self.assertNotEqual(p.returncode,0);self.assertIn('require exactly ISO C11',p.stdout+p.stderr)
    def test_later_gnu11_override_fails(self):
        p=self.build('int fixture(void) {return 0;}\n','set_source_files_properties(fixture.c PROPERTIES COMPILE_OPTIONS -std=gnu11)')
        self.assertNotEqual(p.returncode,0);self.assertIn('GNU extensions disabled',p.stdout+p.stderr)
    def test_gnu_extension_fails(self):
        p=self.build('int fixture(void) { return ({ int x = 1; x; }); }\n');self.assertNotEqual(p.returncode,0)
    def test_header_only_contract_compiled(self):
        (self.root/'port.h').write_text('typedef struct { void *ctx; int (*read)(void *ctx); } port_t;\n')
        p=self.build('#include "port.h"\n_Static_assert(sizeof(port_t) > 0, "port");\n');self.assertEqual(p.returncode,0,p.stdout+p.stderr)
    def test_header_extension_fails(self):
        (self.root/'port.h').write_text('static inline int bad(void) { return ({ 1; }); }\n')
        p=self.build('#include "port.h"\n');self.assertNotEqual(p.returncode,0)
