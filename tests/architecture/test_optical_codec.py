"""Compile and run real pure-C codec against the shared JSON fixtures."""
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class OpticalCodec(unittest.TestCase):
 def test_quality_abi_and_raw_only_runtime_binding(self):
  header=(ROOT/'firmware/v4-idf61/components/nocti_v4_optical/include/nocti_v4_optical.h').read_text()
  body=re.search(r'typedef enum \{([^}]+)\} nocti_v4_slot_quality_t;',header,re.S).group(1)
  self.assertEqual(re.findall(r'NOCTI_V4_SLOT_QUALITY_(\w+)',body),['INVALID_UNDER_K','MINIMUM_VALID','TARGET_VALID','ABORTED_BY_STOP','STARTUP_WARMUP'])
  codec=(ROOT/'components/nn2_core/include/nn2_core_optical_codec.h').read_text()
  for name,value in [('INVALID_QUALITY',0),('VALID_MINIMUM',1),('VALID_TARGET',2),('ABORTED_BY_STOP',3),('STARTUP_WARMUP',4)]:
   self.assertRegex(codec,r'NN2_OC_'+name+r'='+str(value)+r'\b')
  for directory in ['main','firmware/v4-idf61','components']:
   for path in (ROOT/directory).rglob('*'):
    if path.suffix not in ('.c','.h') or any(p in path.parts for p in ('build','managed_components')) or path.name in ('nn2_core_optical_codec.c','nn2_core_optical_codec.h'):continue
    if path.relative_to(ROOT).as_posix() in ('components/nn2_core/include/nn2_core_raw_buffer.h','components/nn2_core/src/nn2_core_raw_buffer.c'):continue
    self.assertNotIn('nn2_oc_',path.read_text(encoding='utf-8'),str(path))
 def test_shared_vectors_in_c(self):
  cc=os.environ.get('NN2_TEST_CC') or shutil.which('cc') or shutil.which('gcc')
  self.assertTrue(cc,'Native C compiler required; missing compiler is not PASS')
  with tempfile.TemporaryDirectory() as directory:
   d=Path(directory);source=d/'vectors.c';exe=d/('codec.exe' if os.name=='nt' else 'codec')
   subprocess.run([sys.executable,str(ROOT/'test/golden/generate_c_vectors.py'),str(ROOT/'test/golden/optical_frame_vectors.json'),str(source)],check=True)
   subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT/'components/nn2_core/include'),str(ROOT/'components/nn2_core/src/nn2_core_optical_codec.c'),str(source),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
