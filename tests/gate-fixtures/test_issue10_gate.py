#!/usr/bin/env python3
"""Issue #10 characterization fixtures. Run manually: python3 -m unittest discover -s tests/gate-fixtures -v

No hardware, no IDF required; temporary directories only.
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

GATE = pathlib.Path(__file__).resolve().parents[2] / 'tools' / 'class-s-gate.py'
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
        p = subprocess.run([sys.executable, str(GATE), str(self.root)], capture_output=True, text=True)
        self.assertEqual(p.returncode, expected, p.stdout + '\n' + p.stderr)

    def inject(self, layer, text):
        p = self.root / 'components' / layer / 'src' / 'fixture.c'
        p.write_text(p.read_text() + text)

    def cmake(self, text, layer='nn2_core'):
        (self.root / 'components' / layer / 'CMakeLists.txt').write_text(text)

    def test_n2_quoted_keywords(self):
        for keyword in ('REQUIRES', 'PRIV_REQUIRES'):
            with self.subTest(keyword=keyword):
                self.cmake(f'idf_component_register(SRCS "src/fixture.c" "{keyword}" esp_wifi)')
                self.gate(1)

    def test_n2_alternative_mutations(self):
        for command in ('idf_component_set_property(nn2_core REQUIRES esp_wifi APPEND)',
                        'target_link_options(nn2_core INTERFACE -lesp_wifi)',
                        'cmake_language(CALL idf_component_set_property nn2_core REQUIRES esp_wifi)',
                        'custom_dependency_macro(esp_wifi)',
                        'while(TRUE)\nendwhile()',
                        'macro(hidden)\nidf_component_register(REQUIRES esp_wifi)\nendmacro()'):
            with self.subTest(command=command):
                self.cmake('idf_component_register(SRCS "src/fixture.c")\n' + command)
                self.gate(1)

    def test_n2_malformed_or_duplicate_registration(self):
        for text in ('idf_component_register(SRCS "src/fixture.c"))',
                     'idf_component_register(SRCS nested(foo))',
                     'idf_component_register(SRCS "src/fixture.c") trailing',
                     'idf_component_register()\nidf_component_register(REQUIRES esp_wifi)',
                     'idf_component_register(SRCS "src/fixture.c" REQUIRES REQUIRES)'):
            with self.subTest(text=text):
                self.cmake(text)
                self.gate(1)

    def test_n2_dynamic_expression_variants(self):
        for value in ('$ENV{DEPS}', '$CACHE{DEPS}', '$<IF:1,esp_wifi,nn2_core>',
                      'nn2_core;esp_wifi', r'nn2_core\;esp_wifi'):
            with self.subTest(value=value):
                self.cmake(f'idf_component_register(REQUIRES {value})')
                self.gate(1)

    def test_n2_bracket_comments(self):
        self.cmake('#[=[\nidf_component_register(REQUIRES esp_wifi)\n]=]\n'
                   'IDF_COMPONENT_REGISTER(SRCS "src/fixture.c")')
        self.gate(0)

    def test_n2_bracket_arguments_unsupported(self):
        for value in ('[[esp_wifi]]', '[=[esp_wifi]=]'):
            with self.subTest(value=value):
                self.cmake(f'idf_component_register(REQUIRES {value})')
                self.gate(1)

    def test_n2_unsupported_escape_or_token(self):
        for value in (r'REQUIRES esp_\wifi', 'requires esp_wifi', 'UNKNOWN esp_wifi'):
            with self.subTest(value=value):
                self.cmake(f'idf_component_register({value})')
                self.gate(1)

    def test_n2_external_source_paths(self):
        (self.root / 'outside.c').write_text('#include <esp_wifi.h>\n')
        for value in ('SRCS ../../outside.c', 'SRC_DIRS ../../',
                      'SRCS /tmp/outside.c', 'SRCS C:/outside.c'):
            with self.subTest(value=value):
                self.cmake(f'idf_component_register({value})')
                self.gate(1)

    def test_n2_nested_cmake_not_ignored(self):
        (self.root / 'components/nn2_core/src/CMakeLists.txt').write_text(
            'idf_component_register(REQUIRES esp_wifi)')
        self.gate(1)

    def test_n2_component_manager_manifest_fails_closed(self):
        (self.root / 'components/nn2_core/idf_component.yml').write_text(
            'dependencies:\n  espressif/esp_wifi: "*"\n')
        self.gate(1)

    def test_n1_prefixed_platform_paths(self):
        p = self.root / 'components/nn2_core/src/fixture.c'
        for inc in ('vendor/driver/gpio.h', '../hal/gpio_hal.h',
                    'vendor/host/ble_gap.h', '../lwip/sockets.h', 'driver//gpio.h'):
            with self.subTest(include=inc):
                p.write_text(f'#include "{inc}"\n')
                self.gate(1)

    def test_n1_include_lexical_forms(self):
        p = self.root / 'components/nn2_core/src/fixture.c'
        for directive in ('#include /* explanation */ <esp_wifi.h>\n',
                          '#inc\\\nlude <esp_wifi.h>\n',
                          '#include <esp_\\\nwifi.h>\n',
                          '%:include <esp_wifi.h>\n'):
            with self.subTest(directive=directive):
                p.write_text(directive)
                self.gate(1)

    def test_n1_commented_includes_not_violations(self):
        self.inject('nn2_core', '/*\n#include <esp_wifi.h>\n*/\n'
                    'const char *example = "/* not a comment */";\n')
        self.gate(0)

    def test_n1_relative_cross_layer_paths(self):
        self.inject('nn2_core', '#include "../../nn2_ports/include/adc.h"\n')
        self.gate(1)

    def test_n1_alternative_source_extensions(self):
        for suffix in ('.cc', '.cxx', '.hh', '.hxx', '.S'):
            with self.subTest(suffix=suffix):
                p = self.root / ('components/nn2_core/src/hidden' + suffix)
                p.write_text('#include <esp_wifi.h>\n')
                self.cmake(f'idf_component_register(SRCS "src/hidden{suffix}")')
                try:
                    self.gate(1)
                finally:
                    p.unlink()

    def test_n4_symlink_directory_and_broken_link(self):
        for broken in (False, True):
            with self.subTest(broken=broken):
                target = self.root / ('missing' if broken else 'outside')
                if not broken:
                    target.mkdir()
                    (target / 'hidden.h').write_text('#include <esp_log.h>')
                link = self.root / 'components/nn2_core/linked'
                link.symlink_to(target, target_is_directory=True)
                try:
                    self.gate(1)
                finally:
                    link.unlink()

    def test_n4_symlink_layer_and_components(self):
        for name in ('components/nn2_core', 'components'):
            with self.subTest(path=name):
                original = self.root / name
                target = self.root / 'saved'
                original.rename(target)
                original.symlink_to(target, target_is_directory=True)
                try:
                    self.gate(1)
                finally:
                    original.unlink()
                    target.rename(original)

    def test_unknown_component_paths(self):
        for kind in ('file', 'directory', 'broken-link'):
            with self.subTest(kind=kind):
                p = self.root / 'components/nn2_hidden'
                if kind == 'file': p.write_text('hidden')
                elif kind == 'directory': p.mkdir()
                else: p.symlink_to(self.root / 'missing')
                try:
                    self.gate(1)
                finally:
                    if kind == 'directory': p.rmdir()
                    else: p.unlink()

    def test_unreadable_source_fails(self):
        (self.root / 'components/nn2_core/src/fixture.c').write_bytes(b'\xff')
        self.gate(1)

    def test_scanner_enumeration_error_fails(self):
        import importlib.util
        from unittest.mock import patch
        spec = importlib.util.spec_from_file_location('gate', GATE)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with patch.object(module.os, 'scandir', side_effect=PermissionError('fixture')):
            with patch.object(sys, 'argv', [str(GATE), str(self.root)]):
                self.assertEqual(module.main(), 1)

    def test_missing_root_and_non_directory_components_fail(self):
        import shutil
        shutil.rmtree(self.root / 'components')
        (self.root / 'components').write_text('invalid hierarchy')
        self.gate(1)
        p = subprocess.run([sys.executable, str(GATE), str(self.root / 'missing')],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 1, p.stdout + p.stderr)

    def test_valid(self):
        self.gate(0)

    def test_n1_complete_adr_forbidden_matrix(self):
        headers = ('esp_log.h', 'freertos/task.h', 'driver/gpio.h', 'lwip/sockets.h',
                   'hal/gpio_hal.h', 'soc/gpio_reg.h', 'rom/ets_sys.h', 'esp_private/foo.h',
                   'sdkconfig.h', 'nimble/ble.h', 'host/ble_hs.h', 'bt_types.h',
                   'nvs.h', 'nvs_flash.h', 'nvs_handle.h', 'FreeRTOS.h', 'task.h',
                   'queue.h', 'semphr.h', 'timers.h', 'event_groups.h',
                   'nn2_adapter_adc.h', 'nn2_adapters_adc.h')
        for layer in LAYERS:
            p = self.root / 'components' / layer / 'src/fixture.c'
            for header in headers:
                with self.subTest(layer=layer, header=header):
                    p.write_text(f'#include <{header}>\n')
                    self.gate(1)
            p.write_text('#include <stdint.h>\n')

    def test_n1_cross_layer_matrix(self):
        for layer, header in (('nn2_core', 'nn2_app_session.h'),
                              ('nn2_core', 'nn2_port_adc.h'),
                              ('nn2_ports', 'nn2_app_session.h')):
            with self.subTest(layer=layer, header=header):
                p = self.root / 'components' / layer / 'src/fixture.c'
                p.write_text(f'#include "prefix/{header}"\n')
                self.gate(1)
                p.write_text('#include <stdint.h>\n')

    def test_n2_allowed_quoted_private_and_multiline_dependencies(self):
        self.cmake('idf_component_register(\n INCLUDE_DIRS "src"\n "PRIV_REQUIRES" "nn2_core"\n)',
                   'nn2_ports')
        self.cmake('idf_component_register(SRCS "src/fixture.c" REQUIRES "nn2_core"\n'
                   'PRIV_REQUIRES nn2_ports)', 'nn2_app')
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

    def test_n2_comment_fake_dependency_is_ignored(self):
        p = self.root / 'components' / 'nn2_core' / 'CMakeLists.txt'
        p.write_text('idf_component_register(SRCS "src/fixture.c" # REQUIRES esp_wifi\n)\n')
        self.gate(0)

    def test_n2_quoted_hash_preserved(self):
        p = self.root / 'components' / 'nn2_core' / 'CMakeLists.txt'
        p.write_text('idf_component_register(SRCS "src/#fixture.c")\n')
        self.gate(0)

    def test_n2_dynamic_requires_fails_closed(self):
        p = self.root / 'components' / 'nn2_app' / 'CMakeLists.txt'
        p.write_text('idf_component_register(SRCS "src/fixture.c" REQUIRES ${DYNAMIC_DEPS})\n')
        self.gate(1)

    def test_n2_cmake_semicolon_list_fails_closed(self):
        p = self.root / 'components' / 'nn2_app' / 'CMakeLists.txt'
        p.write_text('idf_component_register(SRCS "src/fixture.c" REQUIRES "nn2_core;esp_wifi")\n')
        self.gate(1)

    def test_n2_register_in_comment_not_counted(self):
        p = self.root / 'components' / 'nn2_core' / 'CMakeLists.txt'
        p.write_text('# idf_component_register(REQUIRES esp_wifi)\nidf_component_register(SRCS "src/fixture.c")\n')
        self.gate(0)

    def test_n2_quoted_dependency_is_checked(self):
        p = self.root / 'components' / 'nn2_app' / 'CMakeLists.txt'
        p.write_text('idf_component_register(SRCS "src/fixture.c" PRIV_REQUIRES "esp_wifi")\n')
        self.gate(1)

    def test_n2_unterminated_quote_fails_closed(self):
        p = self.root / 'components' / 'nn2_core' / 'CMakeLists.txt'
        p.write_text('idf_component_register(SRCS "src/fixture.c)\n')
        self.gate(1)

    def test_n2_external_include_fails_closed(self):
        p = self.root / 'components' / 'nn2_core' / 'CMakeLists.txt'
        p.write_text('include(external.cmake)\nidf_component_register(SRCS "src/fixture.c")\n')
        self.gate(1)

    def test_n4_symlink_file_fails_closed(self):
        p = self.root / 'components' / 'nn2_core' / 'src' / 'fixture.c'
        p.unlink()
        target = self.root / 'outside.c'
        target.write_text('#include <esp_log.h>\n')
        p.symlink_to(target)
        self.gate(1)


class WorkflowDetectionFixtures(unittest.TestCase):
    """Execute the actual workflow shell block, including skip/bypass edge cases."""
    def setUp(self):
        import textwrap
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        workflow = (GATE.parents[1] / '.github/workflows/firmware-build.yml').read_text()
        block = workflow.split('      - name: Detect layered components', 1)[1]
        self.script = textwrap.dedent(block.split('        run: |\n', 1)[1]
                                     .split('\n  static-dependency-gate:', 1)[0])

    def detect(self, expected, present=None):
        import os
        env = dict(os.environ, GITHUB_OUTPUT='.detect-output', GITHUB_STEP_SUMMARY='.detect-summary')
        p = subprocess.run(['bash', '-c', self.script], cwd=self.root, env=env,
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, expected, p.stdout + p.stderr)
        if present is not None:
            self.assertIn('present=' + present, (self.root / '.detect-output').read_text())

    def test_absent(self):
        self.detect(0, 'false')

    def test_present(self):
        scaffold(self.root)
        self.detect(0, 'true')

    def test_components_symlink(self):
        target = self.root / 'outside'
        target.mkdir()
        (self.root / 'components').symlink_to(target, target_is_directory=True)
        self.detect(1)

    def test_broken_normative_symlink(self):
        (self.root / 'components').mkdir()
        (self.root / 'components/nn2_core').symlink_to(self.root / 'missing', target_is_directory=True)
        self.detect(1)

    def test_normative_file(self):
        (self.root / 'components').mkdir()
        (self.root / 'components/nn2_core').write_text('invalid')
        self.detect(1)

    def test_unknown_file_and_symlink(self):
        (self.root / 'components').mkdir()
        p = self.root / 'components/nn2_hidden'
        for linked in (False, True):
            with self.subTest(linked=linked):
                if linked: p.symlink_to(self.root / 'missing')
                else: p.write_text('invalid')
                try:
                    self.detect(1)
                finally:
                    p.unlink()


if __name__ == '__main__':
    unittest.main()
