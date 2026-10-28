import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import layer_contract


class PlatformBoundaryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / 'tools').mkdir()
        (self.root / 'components/nn2_board').mkdir(parents=True)
        self.rows = layer_contract.load_manifest()

    def write(self, paths):
        (self.root / 'tools/platform_components.json').write_text(
            json.dumps(dict(schema_version=1, paths=paths)))

    def test_unknown_stays_rejected(self):
        with self.assertRaises(ValueError):
            layer_contract.detect(self.root, self.rows)

    def test_explicit_platform_is_not_an_inner_layer(self):
        self.write(['components/nn2_board'])
        self.assertEqual(layer_contract.detect(self.root, self.rows), [])

    def test_traversal_and_reclassification_rejected(self):
        for paths in (['../components/nn2_board'], ['components/nn2_core'],
                      ['components/nn2_board', 'components/nn2_board']):
            self.write(paths)
            with self.assertRaises(ValueError):
                layer_contract.detect(self.root, self.rows)

    def test_actual_inner_policy_remains_clean(self):
        spec = importlib.util.spec_from_file_location('class_s', ROOT / 'tools/class-s-gate.py')
        gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gate)
        errors, present = gate.audit(ROOT, layer_contract.MANIFEST)
        self.assertTrue(present)
        self.assertEqual(errors, [])

    def test_core_cannot_import_registered_platform(self):
        self.write(['components/nn2_board'])
        core = self.root / 'components/nn2_core'
        core.mkdir()
        (core / 'bad.c').write_text('#include "nn2_board.h"\n')
        (core / 'CMakeLists.txt').write_text('idf_component_register(SRCS "bad.c" INCLUDE_DIRS ".")\n')
        spec = importlib.util.spec_from_file_location('class_s_negative', ROOT / 'tools/class-s-gate.py')
        gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gate)
        errors, present = gate.audit(self.root, layer_contract.MANIFEST)
        self.assertTrue(present)
        self.assertTrue(any('forbidden include nn2_board.h' in error for error in errors))


if __name__ == '__main__':
    unittest.main()
