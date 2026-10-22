"""CI entry point for the schema v2 suite. The cases live in tools/tests."""
import importlib.util
from pathlib import Path

_PATH = Path(__file__).resolve().parents[2] / 'tools' / 'tests' / 'test_layers_v2.py'
_SPEC = importlib.util.spec_from_file_location('layers_v2_suite', _PATH)
_MODULE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_MODULE)
LayersV2Tests = _MODULE.LayersV2Tests
