"""Build contract fixtures; not product Class U or hardware evidence."""
import os
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
        p=subprocess.run(['cmake','-S',str(self.root),'-B',str(self.root/'build'),'-G','Ninja','-DCMAKE_C_COMPILER='+self.cc,'-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        return subprocess.run(['cmake','--build',str(self.root/'build'),'--verbose'],capture_output=True,text=True)
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
