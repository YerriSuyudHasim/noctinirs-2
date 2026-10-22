"""N6 integration regressions: synthetic fixtures, never product conformance."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from layer_contract import load_contract


class N6Tests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.manifest = self.root / 'policy.json'
        self.policy = load_contract()
        self.write_policy()
        for row in self.policy['layers']:
            d = self.root / row['path']
            (d / 'include').mkdir(parents=True)
            (d / 'include/fixture.h').write_text('#include <stdint.h>\n')
            deps = ' REQUIRES ' + ' '.join(row['requires']) if row['requires'] else ''
            (d / 'CMakeLists.txt').write_text('idf_component_register(INCLUDE_DIRS "include"' + deps + ')\n')

    def write_policy(self):
        self.manifest.write_text(json.dumps(self.policy), encoding='utf-8')

    def gate(self, expected):
        p = subprocess.run([sys.executable, str(ROOT / 'tools/class-s-gate.py'),
                            str(self.root), '--manifest', str(self.manifest)], capture_output=True, text=True)
        self.assertEqual(p.returncode, expected, p.stdout + p.stderr)
        return p.stdout + p.stderr

    def test_canonical_dependency_oracle(self):
        self.assertEqual({r['name']: r['requires'] for r in self.policy['layers']},
                         {'core': [], 'ports': ['nn2_core'], 'app': ['nn2_core', 'nn2_ports']})
        self.gate(0)

    def test_missing_manifest_before_absence(self):
        self.manifest.unlink()
        shutil.rmtree(self.root / 'components')
        self.assertNotIn('NOT APPLICABLE', self.gate(1))

    def test_malformed_manifest_before_absence(self):
        shutil.rmtree(self.root / 'components')
        for value in ('{', 'null', '{"schema_version":2,"schema_version":2}'):
            with self.subTest(value=value):
                self.manifest.write_text(value)
                self.assertNotIn('NOT APPLICABLE', self.gate(1))

    def test_unknown_layer(self):
        (self.root / 'components/nn2_hidden').mkdir()
        self.gate(1)

    def test_core_forbidden_edges(self):
        for dep in ('nn2_ports', 'nn2_app', 'esp_timer'):
            with self.subTest(dep=dep):
                (self.root / 'components/nn2_core/CMakeLists.txt').write_text('idf_component_register(REQUIRES ' + dep + ')')
                self.gate(1)

    def test_ports_forbidden_edges(self):
        for dep in ('nn2_app', 'driver'):
            with self.subTest(dep=dep):
                (self.root / 'components/nn2_ports/CMakeLists.txt').write_text('idf_component_register(PRIV_REQUIRES ' + dep + ')')
                self.gate(1)

    def test_app_both_valid_dependencies(self):
        (self.root / 'components/nn2_app/include/fixture.h').write_text(
            '#include "nn2_core_types.h"\n#include "nn2_port_adc.h"\n')
        self.gate(0)

    def test_headers_include_violations(self):
        for include in ('esp_wifi.h', '../../nn2_ports/include/fixture.h', 'nn2_ports_alias.h', 'vendor/driver/gpio.h'):
            with self.subTest(include=include):
                (self.root / 'components/nn2_core/include/fixture.h').write_text('#include "' + include + '"\n')
                self.gate(1)

    def test_hidden_build_dependencies(self):
        for text in ('idf_component_register(REQUIRES ${HIDDEN})',
                     'idf_component_register()\ninclude(hidden.cmake)',
                     'idf_component_register(INCLUDE_DIRS ../../outside)'):
            with self.subTest(text=text):
                (self.root / 'components/nn2_core/CMakeLists.txt').write_text(text)
                self.gate(1)

    def test_duplicate_and_conflicting_layers(self):
        for change in ('duplicate', 'prefix', 'cycle', 'self', 'unknown', 'duplicate-dep'):
            with self.subTest(change=change):
                self.policy = load_contract()
                if change == 'duplicate': self.policy['layers'][1] = copy.deepcopy(self.policy['layers'][0])
                elif change == 'prefix': self.policy['layers'][1]['header_prefixes'] = ['nn2_core_']
                elif change == 'cycle': self.policy['layers'][0]['requires'] = ['nn2_ports']
                elif change == 'self': self.policy['layers'][0]['requires'] = ['nn2_core']
                elif change == 'unknown': self.policy['layers'][0]['requires'] = ['nn2_hidden']
                else: self.policy['layers'][1]['requires'] *= 2
                self.write_policy()
                self.gate(1)

    def test_missing_source_directory(self):
        shutil.rmtree(self.root / 'components/nn2_ports')
        self.gate(1)

    def test_no_normative_source_files(self):
        for path in (self.root / 'components').rglob('*.h'): path.unlink()
        self.assertIn('empty source/header layer', self.gate(1))

    def test_absent_layers_not_applicable(self):
        shutil.rmtree(self.root / 'components')
        result = self.gate(3)
        self.assertIn('NOT APPLICABLE', result)
        self.assertNotIn('PASS', result)

    def test_deterministic_repeated_results(self):
        (self.root / 'components/nn2_core/include/z.h').write_text('#include <esp_wifi.h>')
        (self.root / 'components/nn2_core/include/a.h').write_text('#include <driver/gpio.h>')
        self.assertEqual(self.gate(1), self.gate(1))

    def test_all_consumers_use_complete_policy(self):
        # A synthetic stricter policy must alter verdicts: no dormant hardcoded map.
        self.policy['layers'][2]['requires'] = ['nn2_core']
        self.write_policy()
        self.assertIn('forbidden CMake dependency nn2_ports', self.gate(1))
        self.policy = load_contract()
        self.policy['forbidden_include_regex'].append('fixture_forbidden\\.h')
        self.write_policy()
        (self.root / 'components/nn2_core/include/fixture.h').write_text('#include <fixture_forbidden.h>')
        self.gate(1)

    def test_invalid_or_empty_policy_fields(self):
        for field, value in (('source_extensions', []), ('forbidden_include_regex', []),
                             ('forbidden_include_regex', ['[']), ('source_extensions', ['../c'])):
            with self.subTest(field=field, value=value):
                self.policy = load_contract(); self.policy[field] = value
                self.write_policy(); self.gate(1)

    def test_extension_and_prefix_policy_are_live(self):
        # Independent stricter policies must change the actual scanner verdict.
        header = self.root / 'components/nn2_core/include/fixture.h'
        header.write_text('#include "nn2_service_private.h"\n')
        self.gate(0)
        self.policy['layers'][2]['header_prefixes'].append('nn2_service_')
        self.write_policy()
        self.assertIn('forbidden include', self.gate(1))
        header.write_text('#include <stdint.h>\n')
        extra = header.with_suffix('.fixture')
        extra.write_text('#include <esp_wifi.h>\n')
        self.gate(0)
        self.policy['source_extensions'].append('.fixture')
        self.write_policy()
        self.assertIn('forbidden include', self.gate(1))

    def workflow_script(self, start, end=None):
        workflow = (ROOT / '.github/workflows/firmware-build.yml').read_text()
        block = workflow.split(start, 1)[1].split('        run: |\n', 1)[1]
        if end: block = block.split(end, 1)[0]
        return textwrap.dedent(block)

    def shell(self, script, **values):
        env = dict(os.environ, GITHUB_STEP_SUMMARY=str(self.root / 'summary'), **values)
        return subprocess.run(['bash', '-c', script], cwd=self.root, env=env, capture_output=True, text=True)

    def test_ci_invariant_verdict_matrix(self):
        script = self.workflow_script('      - name: Check Class S gate invariant')
        for detect, present, gate, expected in (
            ('success', 'true', 'success', 0), ('success', 'false', 'skipped', 0),
            ('failure', '', 'skipped', 1), ('cancelled', 'true', 'skipped', 1),
            ('success', 'true', 'skipped', 1), ('success', 'true', 'failure', 1),
            ('success', 'false', 'success', 1), ('success', '', 'skipped', 1)):
            with self.subTest(detect=detect, present=present, gate=gate):
                p = self.shell(script, DETECT_RESULT=detect, PRESENT=present, GATE_RESULT=gate)
                self.assertEqual(p.returncode, expected, p.stdout + p.stderr)

    def test_ci_gate_exit_propagation(self):
        script = self.workflow_script('      - name: Forbidden-include gate', '\n  class-s-gate-selftest:')
        (self.root / 'tools').mkdir()
        wrapper = self.root / 'tools/class-s-gate.sh'
        for rc in (0, 1, 3, 2, 127):
            with self.subTest(rc=rc):
                wrapper.write_text('exit ' + str(rc) + '\n')
                p = self.shell(script)
                self.assertEqual(p.returncode, 0 if rc == 0 else 1, p.stdout + p.stderr)

    def test_q11_residual_risks_remain_explicit(self):
        text = (ROOT / 'docs/adr/0001-clean-architecture-boundaries.md').read_text()
        for token in ('| Status | **Accepted** (bounded static architecture framework;',
                      'G3', 'G4', 'G5', '__has_include', '-std=c11', '<stdatomic.h>',
                      '**R1/R2 retain only their', '**CLOSED**',
                      'NOT APPLICABLE (not evidence)', 'class-u-host-tests',
                      '**15368**', '**2026-10-25T01:00:00Z**',
                      'isolated C11 host builds', 'Each atomic use needs its own RTOS/target review',
                      'No issue is closed by this record'):
            self.assertIn(token, text)
        # Characterize the known macro blind spot; this is no conformance claim.
        (self.root / 'components/nn2_core/include/fixture.h').write_text(
            '#define HIDDEN <esp_wifi.h>\n#include HIDDEN\n')
        self.gate(0)
