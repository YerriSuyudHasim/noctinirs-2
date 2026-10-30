"""Offline fixture oracle only. No production gate implementation in Batch 2."""
import json
import math
from pathlib import Path
import struct
import unittest
ROOT=Path(__file__).resolve().parents[2]
class OpticalGateVectors(unittest.TestCase):
 def test_wire_arithmetic_and_ieee_bits(self):
  data=json.loads((ROOT/'test/golden/optical_gate_vectors.json').read_text())
  self.assertEqual(data['schema'],'noctinirs.optical_gate_vectors/1')
  for vector in data['vectors']:
   with self.subTest(vector=vector['name']):
    w=vector['wire'];p=vector['parameters'];e=vector['expected']
    if not w['n_on'] or not w['n_off'] or -2147483648 in (w['sum_on'],w['sum_off']) or 65535 in (w['sd_on_q4'],w['sd_off_q4']):
     self.assertEqual(e,dict(STEP_Z_PASS=False,FLOOR_PASS=False,STABLE_PASS=False,diff_f64_hex=None,thr_f64_hex=None));continue
    a=float(w['sum_on'])/float(w['n_on'])
    b=float(w['sum_off'])/float(w['n_off'])
    diff=a-b
    s_on=float(w['sd_on_q4'])/16.0
    s_off=float(w['sd_off_q4'])/16.0
    v_on=s_on*s_on/float(w['n_on'])
    v_off=s_off*s_off/float(w['n_off'])
    se=math.sqrt(v_on+v_off)
    floor_se=float(p['se_floor_x100'])/100.0
    se_eff=se if se>floor_se else floor_se
    z=float(p['z_x10'])/10.0
    thr=z*se_eff
    self.assertEqual(struct.pack('>d',diff).hex(),e['diff_f64_hex'])
    self.assertEqual(struct.pack('>d',thr).hex(),e['thr_f64_hex'])
    self.assertEqual(diff>=thr,e['STEP_Z_PASS'])
    self.assertEqual(diff>=float(p['floor_counts']),e['FLOOR_PASS'])
    self.assertEqual(w['stab_q']!=255 and 10*w['stab_q']<=16*p['stable_k_x10'],e['STABLE_PASS'])
 def test_threshold_triplet_is_sensitive(self):
  vectors={v['name']:v for v in json.loads((ROOT/'test/golden/optical_gate_vectors.json').read_text())['vectors']}
  exact=vectors['exact_step_threshold']['expected']
  self.assertEqual(exact['diff_f64_hex'],exact['thr_f64_hex'])
  self.assertTrue(exact['STEP_Z_PASS'])
  self.assertTrue(vectors['one_sd_lsb_below']['expected']['STEP_Z_PASS'])
  self.assertFalse(vectors['one_sd_lsb_above']['expected']['STEP_Z_PASS'])
