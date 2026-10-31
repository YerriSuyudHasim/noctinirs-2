"""Contract assets; static source checks, no target claims."""
import collections
import copy
import importlib.util
import json
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('geometry', ROOT/'tools/optical_geometry.py')
geometry=importlib.util.module_from_spec(spec);spec.loader.exec_module(geometry)
def load(p): return json.loads((ROOT/p).read_text(encoding='utf-8'))
class OpticalGeometry(unittest.TestCase):
 def test_schema_and_preserved_draft(self):
  g=load('config/optical_geometry_v1.json');s=load('config/optical_geometry.schema.json')
  geometry.validate_schema(g,s)
  original=load('docs/drafts/optical_geometry_v1.draft.json')
  self.assertEqual(g.pop('source_draft_status'),original['status']);g['status']=original['status'];self.assertEqual(g,original)
 def test_schema_rejects_malformed(self):
  g=load('config/optical_geometry_v1.json');s=load('config/optical_geometry.schema.json')
  for change in [lambda x:x.pop('sources'),lambda x:x.update(extra=1),lambda x:x['channels'][0].update(ain=4),lambda x:x['sources'][0].update(side='X')]:
   bad=copy.deepcopy(g);change(bad)
   with self.assertRaises(ValueError):geometry.validate_schema(bad,s)
 def test_channels_sides_mapping_and_spacing_bounds(self):
  g=load('config/optical_geometry_v1.json');channels=g['channels'];sources={s['id']:s for s in g['sources']};pd={d['pd']:d for d in g['detectors']}
  self.assertEqual(len(channels),20);self.assertEqual(len({c['name'] for c in channels}),20)
  self.assertEqual(len({(c['source'],c['pd']) for c in channels}),20)
  self.assertEqual(collections.Counter(c['source'] for c in channels),{s:2 for s in sources})
  for c in channels:
   self.assertEqual(sources[c['source']]['side'],pd[c['pd']]['side'])
   self.assertEqual(geometry.ain_to_pd(c['ain'],g),c['pd'])
  # Triangle inequalities: inferred bounds, NOT a caliper measurement.
  for side,pair in [('L',{'PD1','PD2'}),('R',{'PD4','PD3'})]:
   distances=[[c['sd_mm'] for c in channels if c['source']==s] for s in sources if sources[s]['side']==side]
   self.assertEqual({d['pd'] for d in g['detectors'] if d['side']==side},pair)
   lower=max(abs(a-b) for a,b in distances);upper=min(a+b for a,b in distances)
   self.assertAlmostEqual(lower,11.5);self.assertAlmostEqual(upper,13.0)
 def test_c_source_tables_match_json(self):
  g=load('config/optical_geometry_v1.json');expected={}
  for source in g['sources']:
   for wl,slot in source['slot'].items():expected[slot]=(slot,source['software_source'],source['pca_output'][wl],int(wl))
  base=ROOT/'firmware/v4-idf61/components'
  text=(base/'nocti_v4_optical/nocti_v4_optical.c').read_text()
  body=re.search(r's_descriptor\[.*?\]\s*=\s*\{(.*?)\n\};',text,re.S).group(1)
  rows=[list(map(int,re.findall(r'(\d+)u',row))) for row in re.findall(r'\{([^}]+)\}',body)]
  self.assertEqual(len(rows),20)
  self.assertEqual({r[0]:(r[1],r[2],r[3],r[4]) for r in rows},expected)
  led=(base/'nocti_v4_led/nocti_v4_led.c').read_text()
  body=re.search(r'k_source_to_pca\[.*?\]\s*=\s*\{(.*?)\};',led,re.S).group(1)
  mapping=list(map(int,re.findall(r'(\d+)u',body)))
  self.assertEqual(mapping,list(range(10))+list(range(14,24)))
  self.assertEqual(mapping,[expected[i][2] for i in range(20)])
 def test_one_conversion_function(self):
  definitions=[]
  for folder in ['tools','components','firmware/v4-idf61','main']:
   for p in (ROOT/folder).rglob('*'):
    if p.suffix not in ('.c','.h','.py') or any(x in p.parts for x in ('build','managed_components')):continue
    text=p.read_text(encoding='utf-8')
    for match in re.finditer(r'^def (\w*ain_to_pd)\(|^\s*(?:static\s+)?(?:const\s+)?\w+[\s*]+(\w*ain_to_pd)\s*\([^;]*?\)\s*\{',text,re.M):
     definitions.append((p.relative_to(ROOT).as_posix(),match.group(1) or match.group(2)))
  self.assertEqual(definitions,[('tools/optical_geometry.py','ain_to_pd')])
  for ain in range(4):self.assertIn(geometry.ain_to_pd(ain,load('config/optical_geometry_v1.json')),['PD1','PD2','PD3','PD4'])
 def test_gate_configs(self):
  g=load('config/optical_gate_configs.json');s=load('config/optical_gate_configs.schema.json');geometry.validate_schema(g,s)
  self.assertEqual([c['gate_config_id'] for c in g['configs']],[0,1])
  self.assertFalse(g['configs'][0]['enabled']);self.assertTrue(g['configs'][1]['provisional_unmeasured'])
  for key in g['configs'][1]:
   bad=copy.deepcopy(g);bad['configs'][1].pop(key)
   with self.assertRaises(ValueError):geometry.validate_schema(bad,s)
