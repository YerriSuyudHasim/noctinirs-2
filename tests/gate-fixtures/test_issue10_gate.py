#!/usr/bin/env python3
"""Issue #10 characterization fixtures. Run manually: python3 -m unittest discover -s tests/gate-fixtures -v

Intentionally RED on the existing gate: N1/N2/N4 weaknesses are not fixed in phase 1–2.
No hardware, no IDF required; temporary directories only.
"""
import pathlib
import subprocess
import tempfile
import unittest

GATE = pathlib.Path(__file__).resolve().parents[2] / 'tools' / 'class-s-gate.sh'
LAYERS = ('nn2_core', 'nn2_ports', 'nn2_app')


def scaffold(root):
    for layer in LAYERS:
        d = root / 'components' / layer
        (d / 'src').mkdir(parents=True)
        (d / 'src' / 'fixture.c').write_text('#include <stdint.h>\n')
        allowed = {'nn2_core': '', 'nn2_ports': ' REQUIRES nn2_core',
                   'nn2_app': ' REQUIRES nn2_core nn2_ports'}[layer]
        (d / 'CMakeLists.txt').write_text('idf_component_register(SRCS "src/fixture.c"' + allowed + ')\n')


class Issue10GateFixtures(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        scaffold(self.root)

    def gate(self, expected):
        p = subprocess.run(['bash', str(GATE), str(self.root)], capture_output=True, text=True)
        self.assertEqual(p.returncode, expected, p.stdout + '\n' + p.stderr)

    def inject(self, layer, text):
        p = self.root / 'components' / layer / 'src' / 'fixture.c'
        p.write_text(p.read_text() + text)

    def test_valid(self):
        self.gate(0)

    def test_no_layers_not_applicable(self):
        import shutil
        shutil.rmtree(self.root / 'components')
        self.gate(3)

    def test_empty_layer_fails(self):
        (self.root / 'components' / 'nn2_app' / 'src' / 'fixture.c').unlink()
        self.gate(1)

    def test_missing_layer_fails(self):
        import shutil
        shutil.rmtree(self.root / 'components' / 'nn2_ports')
        self.gate(1)

    def test_n1_unprefixed_freertos(self):
        self.inject('nn2_core', '#include <queue.h>\n')
        self.gate(1)

    def test_n1_ble_header(self):
        self.inject('nn2_app', '#include <host/ble_hs.h>\n')
        self.gate(1)

    def test_n1_cross_layer_core_to_ports(self):
        self.inject('nn2_core', '#include "nn2_port_adc.h"\n')
        self.gate(1)

    def test_n1_cross_layer_ports_to_app(self):
        self.inject('nn2_ports', '#include "nn2_app_session.h"\n')
        self.gate(1)

    def test_n1_adapter_plural(self):
        self.inject('nn2_app', '#include "nn2_adapters_adc.h"\n')
        self.gate(1)

    def test_n2_core_requires_idf(self):
        p = self.root / 'components' / 'nn2_core' / 'CMakeLists.txt'
        p.write_text('idf_component_register(SRCS "src/fixture.c" REQUIRES esp_timer)\n')
        self.gate(1)

    def test_n2_ports_priv_requires_illegal(self):
        p = self.root / 'components' / 'nn2_ports' / 'CMakeLists.txt'
        p.write_text('idf_component_register(SRCS "src/fixture.c" PRIV_REQUIRES driver)\n')
        self.gate(1)

    def test_n2_app_multiline_illegal(self):
        p = self.root / 'components' / 'nn2_app' / 'CMakeLists.txt'
        p.write_text('idf_component_register(\n SRCS "src/fixture.c"\n REQUIRES nn2_core\n   esp_wifi\n)\n')
        self.gate(1)

    def test_n4_symlink_file_fails_closed(self):
        p = self.root / 'components' / 'nn2_core' / 'src' / 'fixture.c'
        p.unlink()
        target = self.root / 'outside.c'
        target.write_text('#include <esp_log.h>\n')
        p.symlink_to(target)
        self.gate(1)


if __name__ == '__main__':
    unittest.main()
