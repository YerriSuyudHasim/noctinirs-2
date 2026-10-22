"""Schema v2 outer-row fixtures. Synthetic trees only; not product conformance."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from layer_contract import load_contract

GATE = ROOT / 'tools' / 'class-s-gate.py'
INNERS = ('nn2_core', 'nn2_ports', 'nn2_app')


class LayersV2Tests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.scaffold()

    def scaffold(self):
        sources = {
            'nn2_core': '#include <stdint.h>\n',
            'nn2_ports': '#include "nn2_core_types.h"\n',
            'nn2_app': '#include "nn2_port_led.h"\n',
        }
        requires = {
            'nn2_core': '',
            'nn2_ports': ' REQUIRES nn2_core',
            'nn2_app': ' REQUIRES nn2_core nn2_ports',
        }
        for name in INNERS:
            directory = self.root / 'components' / name
            (directory / 'include').mkdir(parents=True, exist_ok=True)
            (directory / 'include' / 'fixture.h').write_text(sources[name])
            (directory / 'CMakeLists.txt').write_text(
                'idf_component_register(INCLUDE_DIRS "include"' + requires[name] + ')\n')

    def gate(self, manifest=None):
        command = [sys.executable, str(GATE), str(self.root)]
        if manifest is not None:
            command.extend(['--manifest', str(manifest)])
        result = subprocess.run(command, capture_output=True, text=True)
        return result.returncode, result.stdout + result.stderr

    def cmake(self, component, text):
        (self.root / 'components' / component / 'CMakeLists.txt').write_text(text)

    def include(self, component, text, name='fixture.h'):
        (self.root / 'components' / component / 'include' / name).write_text(text)

    def add_component(self, name, cmake, source, header_name=None):
        directory = self.root / 'components' / name
        (directory / 'include').mkdir(parents=True)
        header = header_name or (name + '.h')
        (directory / 'include' / header).write_text(source)
        (directory / 'CMakeLists.txt').write_text(cmake)
        return directory

    def test_inner_layers_cannot_require_an_adapter(self):
        for name in INNERS:
            with self.subTest(component=name):
                self.scaffold()
                self.cmake(name, 'idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_ad_x)\n')
                code, text = self.gate()
                self.assertEqual(code, 1, text)
                self.assertIn(name + ': forbidden CMake dependency nn2_ad_x', text)

    def test_inner_layers_cannot_require_or_include_nn2_rtos(self):
        for name in INNERS:
            with self.subTest(component=name, edge='REQUIRES'):
                self.scaffold()
                self.cmake(name, 'idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_rtos)\n')
                code, text = self.gate()
                self.assertEqual(code, 1, text)
                self.assertIn(name + ': forbidden CMake dependency nn2_rtos', text)
            with self.subTest(component=name, edge='include'):
                self.scaffold()
                self.include(name, '#include "nn2_rtos_config.h"\n')
                code, text = self.gate()
                self.assertEqual(code, 1, text)
                self.assertIn('forbidden include nn2_rtos_config.h', text)

    def test_inner_layers_cannot_include_an_adapter_header(self):
        for name in INNERS:
            with self.subTest(component=name):
                self.scaffold()
                self.include(name, '#include "nn2_ad_x.h"\n')
                code, text = self.gate()
                self.assertEqual(code, 1, text)
                self.assertIn('forbidden include nn2_ad_x.h', text)

    def test_inner_sdkconfig_under_esp_platform_still_fails(self):
        self.include('nn2_core', '#ifdef ESP_PLATFORM\n#include "sdkconfig.h"\n#endif\n')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('forbidden include sdkconfig.h', text)

    def test_unmapped_nn2_foo_fails(self):
        (self.root / 'components' / 'nn2_foo').mkdir()
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('unmapped component: nn2_foo', text)

    def test_adapter_to_adapter_fails(self):
        self.add_component('nn2_ad_b', 'idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_core)\n',
                           '#include "nn2_core_types.h"\n')
        self.add_component('nn2_ad_a',
                           'idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_core nn2_ad_b)\n',
                           '#include "nn2_ad_b.h"\n')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertTrue(
            'nn2_ad_a: forbidden CMake dependency nn2_ad_b' in text
            or 'forbidden include nn2_ad_b.h' in text, text)

    def test_unparseable_cmake_dependency_construct_fails(self):
        self.cmake('nn2_core', 'idf_component_register(INCLUDE_DIRS "include")\ninclude(${DEPS})\n')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('unparseable CMake dependency construct: include', text)

    def test_unresolved_config_variable_in_requires_fails(self):
        self.cmake('nn2_ports',
                   'idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_core ${CONFIG_NN2_FOO})\n')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('unresolved CMake variable in dependency position: ${CONFIG_NN2_FOO}', text)
        self.assertNotIn('NOT APPLICABLE', text)

    def test_if_else_union_keeps_forbidden_branch(self):
        self.cmake('nn2_ports', '''
if(CONFIG_A)
  idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_core nn2_ad_x)
else()
  idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_core)
endif()
''')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('nn2_ports: forbidden CMake dependency nn2_ad_x', text)

    def test_if_without_else_does_not_drop_the_other_value(self):
        self.cmake('nn2_ports', '''
set(reqs nn2_ad_x)
if(CONFIG_OFF)
  set(reqs nn2_core)
endif()
idf_component_register(INCLUDE_DIRS "include" REQUIRES ${reqs})
''')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('nn2_ports: forbidden CMake dependency nn2_ad_x', text)

    def test_conditional_append_is_part_of_the_union(self):
        self.cmake('nn2_ports', '''
set(reqs nn2_core)
if(CONFIG_X)
  list(APPEND reqs nn2_rtos)
endif()
idf_component_register(INCLUDE_DIRS "include" REQUIRES ${reqs})
''')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('nn2_ports: forbidden CMake dependency nn2_rtos', text)

    def test_adapter_may_depend_on_nn2_rtos(self):
        self.add_component(
            'nn2_rtos',
            'idf_component_register(INCLUDE_DIRS "include")\n'
            'target_compile_options(${COMPONENT_LIB} PRIVATE -Wall)\n',
            '#include <stdint.h>\n#ifdef ESP_PLATFORM\n#include "sdkconfig.h"\n#endif\n'
            '#include "freertos/FreeRTOS.h"\n',
            header_name='nn2_rtos_config.h')
        self.add_component(
            'nn2_ad_wifi',
            'idf_component_register(INCLUDE_DIRS "include" '
            'REQUIRES nn2_core nn2_ports nn2_rtos freertos)\n',
            '#include "nn2_rtos_config.h"\n#include "esp_wifi.h"\n#include "nn2_core_types.h"\n')
        code, text = self.gate()
        self.assertEqual(code, 0, text)
        self.assertIn('PASS:', text)

    def test_adapter_if_and_config_define_do_not_fail(self):
        self.add_component(
            'nn2_rtos',
            'idf_component_register(INCLUDE_DIRS "include" REQUIRES freertos)\n',
            '#ifdef ESP_PLATFORM\n#include "sdkconfig.h"\n#endif\n#include "nn2_rtos_config.h"\n',
            header_name='nn2_rtos_config.h')
        cmake = '''idf_component_register(
    SRCS "nn2_ad_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES nn2_core nn2_ports nn2_rtos esp_wifi freertos)
if(NOT CONFIG_NN2_WIFI_BACKOFF_FIRST_MS)
    message(FATAL_ERROR "CONFIG_NN2_WIFI_BACKOFF_FIRST_MS is required")
endif()
if(CONFIG_NN2_WIFI_EXTRA)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NN2_WIFI_EXTRA=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        NN2_WIFI_BACKOFF_FIRST_MS=${CONFIG_NN2_WIFI_BACKOFF_FIRST_MS})
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
'''
        directory = self.add_component('nn2_ad_wifi', cmake, '#include "nn2_port_wifi.h"\n')
        (directory / 'nn2_ad_wifi.c').write_text('#include "nn2_ad_wifi.h"\n#include "nn2_rtos_config.h"\n')
        code, text = self.gate()
        self.assertEqual(code, 0, text)
        self.assertIn('PASS:', text)

    def test_inner_compile_definitions_are_not_dependencies(self):
        self.cmake('nn2_core', '''
idf_component_register(INCLUDE_DIRS "include")
target_compile_definitions(${COMPONENT_LIB} PRIVATE NN2_EXAMPLE=1)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall)
''')
        code, text = self.gate()
        self.assertEqual(code, 0, text)

    def test_nn2_rtos_cannot_depend_on_nn2_core_or_an_adapter(self):
        self.add_component(
            'nn2_rtos',
            'idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_core)\n',
            '#include <stdint.h>\n', header_name='nn2_rtos_config.h')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('nn2_rtos: forbidden CMake dependency nn2_core', text)
        self.cmake('nn2_rtos', 'idf_component_register(INCLUDE_DIRS "include" REQUIRES nn2_ad_x)\n')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('nn2_rtos: forbidden CMake dependency nn2_ad_x', text)

    def test_nn2_rtos_cannot_include_an_nn2_header(self):
        self.add_component(
            'nn2_rtos',
            'idf_component_register(INCLUDE_DIRS "include")\n',
            '#include "nn2_foo.h"\n', header_name='nn2_rtos_config.h')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('forbidden include nn2_foo.h', text)
        self.include('nn2_rtos', '#include "nn2_core_types.h"\n', name='nn2_rtos_config.h')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('forbidden include nn2_core_types.h', text)
        self.include('nn2_rtos', '#include "nn2_ad_x.h"\n', name='nn2_rtos_config.h')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('forbidden include nn2_ad_x.h', text)

    def test_nn2_rtos_priv_requires_nn2_core_fails(self):
        self.add_component(
            'nn2_rtos',
            'idf_component_register(INCLUDE_DIRS "include" PRIV_REQUIRES nn2_core)\n',
            '#include <stdint.h>\n', header_name='nn2_rtos_config.h')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('nn2_rtos: forbidden CMake dependency nn2_core', text)

    def test_nn2_rtos_c_source_include_nn2_core_types_fails(self):
        directory = self.add_component(
            'nn2_rtos',
            'idf_component_register(SRCS "x.c" INCLUDE_DIRS "include")\n',
            '#include <stdint.h>\n', header_name='nn2_rtos_config.h')
        (directory / 'x.c').write_text('#include "nn2_core_types.h"\n')
        code, text = self.gate()
        self.assertEqual(code, 1, text)
        self.assertIn('components/nn2_rtos/x.c: forbidden include nn2_core_types.h', text)

    def test_v1_manifest_still_loads_and_leaves_adapters_unmapped(self):
        policy = load_contract()
        self.assertEqual([row['name'] for row in policy['layers']], ['core', 'ports', 'app'])
        del policy['outer_layers']
        manifest = self.root / 'v1.json'
        manifest.write_text(json.dumps(policy))
        loaded = load_contract(manifest)
        self.assertEqual(len(loaded['layers']), 3)
        self.assertNotIn('outer_layers', loaded)
        self.add_component('nn2_ad_wifi', 'idf_component_register(INCLUDE_DIRS "include")\n',
                           '#include <stdint.h>\n')
        code, text = self.gate(manifest)
        self.assertEqual(code, 1, text)
        self.assertIn('unmapped component: nn2_ad_wifi', text)

    def test_exactly_three_inner_layers_remain_required(self):
        policy = load_contract()
        policy['layers'].append(dict(policy['layers'][0]))
        manifest = self.root / 'four.json'
        manifest.write_text(json.dumps(policy))
        with self.assertRaises(ValueError):
            load_contract(manifest)

    def test_platform_row_cannot_be_weakened_to_allow_nn2_core(self):
        policy = load_contract()
        platform = next(row for row in policy['outer_layers'] if row['name'] == 'platform')
        platform['allowed_dependencies'] = ['nn2_core']
        manifest = self.root / 'weak.json'
        manifest.write_text(json.dumps(policy))
        with self.assertRaises(ValueError):
            load_contract(manifest)


if __name__ == '__main__':
    unittest.main()
