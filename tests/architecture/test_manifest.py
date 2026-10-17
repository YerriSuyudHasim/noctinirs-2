import copy
import importlib.util
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('contract',ROOT/'tools/layer_contract.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
class ManifestTests(unittest.TestCase):
    def setUp(self):
        t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup)
        self.root=Path(t.name);self.path=self.root/'layers.json'
        self.data=json.loads((ROOT/'tools/layers.json').read_text())
    def load(self,data):
        self.path.write_text(json.dumps(data));return c.load_manifest(self.path)
    def test_canonical_paths(self):
        self.assertEqual([x['path'] for x in self.load(self.data)],['components/nn2_core','components/nn2_ports','components/nn2_app'])
    def test_missing_manifest(self):
        with self.assertRaises(OSError):c.load_manifest(self.path)
    def test_malformed_or_duplicate_json(self):
        for s in ('','{','null','{"schema_version":1,"schema_version":1,"layers":[]}'):
            with self.subTest(s=s):
                self.path.write_text(s)
                with self.assertRaises(ValueError):c.load_manifest(self.path)
    def test_invalid_schema(self):
        for d in ([],{},dict(self.data,extra=1),dict(self.data,schema_version=True),dict(self.data,schema_version=2),dict(self.data,layers=[]),dict(self.data,layers={})):
            with self.subTest(d=d),self.assertRaises(ValueError):self.load(d)
    def test_missing_duplicate_unexpected_layers(self):
        for change in ('missing','duplicate','unexpected','wrongpath','extra','badtype','traversal'):
            d=copy.deepcopy(self.data)
            if change=='missing':d['layers'].pop()
            elif change=='duplicate':d['layers'][1]=d['layers'][0]
            elif change=='unexpected':d['layers'][1]['name']='adapter'
            elif change=='wrongpath':d['layers'][1]['path']='components/other'
            elif change=='extra':d['layers'][1]['requires']=[]
            elif change=='badtype':d['layers'][1]=None
            else:d['layers'][1]['path']='../components/nn2_ports'
            with self.subTest(change=change),self.assertRaises(ValueError):self.load(d)
    def test_absent_layers_are_not_conformance(self):
        self.assertEqual(c.detect(self.root,self.load(self.data)),[])
        p=subprocess.run([sys.executable,str(ROOT/'tools/layer_contract.py'),'detect','--root',str(self.root)],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr);self.assertIn('present=false',p.stdout)
        self.assertIn('NOT APPLICABLE (not evidence)',p.stderr);self.assertNotIn('PASS',p.stdout+p.stderr)
    def test_empty_layer_requires_gate(self):
        rows=self.load(self.data);(self.root/rows[0]['path']).mkdir(parents=True)
        self.assertEqual(c.detect(self.root,rows),[rows[0]['path']])
    def test_unknown_file_and_directory_fail(self):
        rows=self.load(self.data);(self.root/'components').mkdir();p=self.root/'components/nn2_surprise';p.write_text('')
        with self.assertRaises(ValueError):c.detect(self.root,rows)
        p.unlink();p.mkdir()
        with self.assertRaises(ValueError):c.detect(self.root,rows)
    def test_invalid_component_root_and_layer_fail(self):
        rows=self.load(self.data);p=self.root/'components';p.write_text('')
        with self.assertRaises(ValueError):c.detect(self.root,rows)
        p.unlink();p.mkdir();(self.root/rows[0]['path']).write_text('')
        with self.assertRaises(ValueError):c.detect(self.root,rows)
    def test_broken_link_fails(self):
        rows=self.load(self.data);(self.root/'components').mkdir()
        try:(self.root/rows[0]['path']).symlink_to(self.root/'absent',target_is_directory=True)
        except OSError:self.skipTest('symlink privilege unavailable')
        with self.assertRaises(ValueError):c.detect(self.root,rows)
    def test_workflow_and_gate_consume_manifest(self):
        s=(ROOT/'.github/workflows/firmware-build.yml').read_text()
        self.assertIn('set -euo pipefail\n          python3 tools/layer_contract.py detect > layer-state.txt',s)
        self.assertIn('cat layer-state.txt >> "$GITHUB_OUTPUT"',s)
        self.assertIn('needs: [layer-detect, static-dependency-gate]',s)
        self.assertNotIn('normative=(',s)
        for row in self.data['layers']:self.assertNotIn(row['path'],s)
        s=(ROOT/'tools/class-s-gate.sh').read_text();self.assertIn('layer_contract.py" paths',s);self.assertNotIn('layers=(components/',s)
    def test_cli_missing_manifest_no_outputs(self):
        p=subprocess.run([sys.executable,str(ROOT/'tools/layer_contract.py'),'detect','--manifest',str(self.path)],capture_output=True,text=True)
        self.assertEqual(p.returncode,1);self.assertEqual(p.stdout,'')
    def test_adr_canonical_reference(self):
        s=(ROOT/'docs/adr/0001-clean-architecture-boundaries.md').read_text()
        self.assertIn('../../tools/layers.json',s);self.assertNotIn('tools/layers.yaml',s);self.assertIn('**Proposed**',s)

    def test_gate_empty_layer_and_missing_config_cannot_pass(self):
        bash=shutil.which('bash')
        if os.name=='nt':bash=str(Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Git/bin/bash.exe')
        if not bash or not Path(bash).is_file():self.fail('Bash required for gate integration')
        toolroot=self.root/'tools';toolroot.mkdir()
        for name in ('class-s-gate.sh','layer_contract.py','layers.json'):
            shutil.copy2(ROOT/'tools'/name,toolroot/name)
        for row in self.data['layers']:(self.root/row['path']).mkdir(parents=True)
        env=dict(os.environ,PYTHON=Path(sys.executable).as_posix())
        command=[bash,str(toolroot/'class-s-gate.sh'),str(self.root)]
        p=subprocess.run(command,env=env,capture_output=True,text=True)
        self.assertEqual(p.returncode,1,p.stdout+p.stderr)
        self.assertIn('no C/C++ sources',p.stdout)
        (toolroot/'layers.json').unlink()
        p=subprocess.run(command,env=env,capture_output=True,text=True)
        self.assertEqual(p.returncode,1,p.stdout+p.stderr)
        self.assertNotIn('NOT APPLICABLE',p.stdout+p.stderr)

    def test_cli_emits_lf_only(self):
        p=subprocess.run([sys.executable,str(ROOT/'tools/layer_contract.py'),'paths'],capture_output=True)
        self.assertEqual(p.returncode,0);self.assertNotIn(b'\r',p.stdout)
