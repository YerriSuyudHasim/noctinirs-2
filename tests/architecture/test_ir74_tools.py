"""IR74 tooling regressions; temporary files and mocks, no compiler or linker."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import check_ads131_config as profiles
import collect_inner_build as collector
import fetch_regression_sources as historical


class ProfileValidation(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / 'sdkconfig'

    def config(self, name):
        flags = ['CONFIG_IDF_TARGET="esp32s3"', 'CONFIG_NN2_PROFILE_' + name + '=y']
        if name == 'COMMUNICATION':
            flags += ['CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288', 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y', 'CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y']
        if name == 'ADS131_DIAGNOSTIC':
            flags += ['CONFIG_' + s + '=y' for s in ('ESPTOOLPY_FLASHMODE_DIO', 'ESPTOOLPY_FLASHSIZE_16MB', 'ESP_CONSOLE_USB_SERIAL_JTAG', 'ESP_TASK_WDT_EN', 'ESP_TASK_WDT_INIT')]
        return '\n'.join(flags) + '\n'

    def test_all_profiles_and_mutual_exclusion(self):
        for profile, name in [('fake', 'FAKE'), ('communication', 'COMMUNICATION'), ('diagnostic', 'ADS131_DIAGNOSTIC')]:
            self.path.write_text(self.config(name))
            profiles.check(self.path, profile=profile)
            for other in ['FAKE', 'COMMUNICATION', 'ADS131_DIAGNOSTIC']:
                if other == name: continue
                self.path.write_text(self.config(name) + 'CONFIG_NN2_PROFILE_' + other + '=y\n')
                with self.assertRaisesRegex(ValueError, 'exactly one'):
                    profiles.check(self.path, profile=profile)

    def test_communication_requirements_are_not_removed(self):
        for token in ['CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288', 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y', 'CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y']:
            self.path.write_text(self.config('COMMUNICATION').replace(token, ''))
            with self.assertRaises(ValueError): profiles.check(self.path, profile='communication')

    def test_default_stays_fake_and_shared_editor_does_not_opt_in(self):
        settings = json.loads((ROOT / '.vscode/settings.json').read_text())
        self.assertEqual(settings['idf.sdkconfigDefaults'], ['sdkconfig.defaults'])
        self.assertFalse(any('NN2_EXPECT_LED_' in a for a in settings['idf.cmakeCompilerArgs']))
        self.assertIn('default NN2_PROFILE_FAKE', (ROOT / 'main/Kconfig.projbuild').read_text())
        self.assertIn('CONFIG_NN2_PROFILE_FAKE=y', (ROOT / 'sdkconfig.defaults').read_text())
        for workflow in ['firmware-build.yml', 'reproducible-build.yml']:
            content = (ROOT / '.github/workflows' / workflow).read_text()
            for profile in ['fake', 'communication', 'diagnostic']:
                self.assertIn('- profile: ' + profile, content)
            self.assertIn('defaults: sdkconfig.defaults;sdkconfig.fake', content)


class CollectorCoverage(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'tools').mkdir()
        shutil.copyfile(ROOT / 'tools/layers.json', self.root / 'tools/layers.json')
        for name in ['core', 'ports', 'app']:
            p = self.root / ('components/nn2_' + name)
            p.mkdir(parents=True)
            (p / 'private.h').write_text('')
            if name != 'ports': (p / 'unit.c').write_text('')
        (self.root / 'components/nn2_board').mkdir()
        (self.root / 'components/nn2_board/board.h').write_text('')
        (self.root / 'components/nn2_board/board.c').write_text('')
        (self.root / 'tools/platform_components.json').write_text(json.dumps({'schema_version':1,'paths':['components/nn2_board']}))
        self.build = self.root / 'build'
        self.build.mkdir()
        headers, sources = collector.inner_inventory(self.root)
        self.rows = [[h, str(self.build / ('probe' + str(i) + '.c'))] for i,h in enumerate(headers)]
        paths = sources | {Path(r[1]) for r in self.rows} | {self.build / ('combined_' + s + '.c') for s in ['forward','reverse']}
        self.commands = [{'file':str(p)} for p in paths]

    def test_platform_excluded_and_both_modes_complete(self):
        headers, sources, selected = collector.select_units(self.root, self.build, 'host', self.rows, self.commands)
        self.assertEqual((len(headers),len(sources),len(selected)), (3,2,7))
        idf_rows = [['component', *r, 'target'] for r in self.rows]
        commands = [c for c in self.commands if 'combined_' not in c['file']]
        self.assertEqual(len(collector.select_units(self.root,self.build,'idf',idf_rows,commands)[2]),5)

    def test_unreadable_inventory_fails_closed(self):
        def denied(*args, **kwargs):
            kwargs['onerror'](PermissionError('fixture enumeration denied'))
        with patch.object(collector.os,'walk',side_effect=denied):
            with self.assertRaisesRegex(PermissionError,'enumeration denied'):
                collector.inner_inventory(self.root)

    def test_missing_duplicate_and_platform_substitution_rejected(self):
        for rows in [self.rows[:-1], self.rows + [self.rows[0]], self.rows[:-1] + [['components/nn2_board/board.h','probe.c']]]:
            with self.assertRaisesRegex(RuntimeError,'header coverage'):
                collector.select_units(self.root,self.build,'host',rows,self.commands)
        for commands in [self.commands[:-1], self.commands + [self.commands[0]]]:
            with self.assertRaisesRegex(RuntimeError,'compile-command coverage'):
                collector.select_units(self.root,self.build,'host',self.rows,commands)
        (self.root / 'components/nn2_app/new.c').write_text('')
        with self.assertRaisesRegex(RuntimeError,'compile-command coverage'):
            collector.select_units(self.root,self.build,'host',self.rows,self.commands)


class HistoricalProvenance(unittest.TestCase):
    def test_mismatched_blob_fails_closed_without_build(self):
        with patch.object(historical.subprocess,'run') as run, patch.object(historical.subprocess,'check_output',return_value=b'wrong bytes'):
            run.return_value.returncode=0
            with self.assertRaisesRegex(RuntimeError,'blob mismatch'):
                historical.ensure('adc')
            self.assertEqual(run.call_count,1)

    def test_missing_commit_requires_exact_sha_fetch_then_checks_bytes(self):
        # Unit fixture is deliberately independent of the caller's Git cache.
        # Real original bytes are checked by ensure's separate provenance probe.
        pins=json.loads((ROOT / 'tools/regression_sources.json').read_text())['adc']
        raw=b'fixture bytes for mock fetch and digest verification\n'
        expected=hashlib.sha256(raw).hexdigest()
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root / 'tools').mkdir()
            fixture=dict(pins,files={'main/adapters/nn2_ads131_esp.c':expected})
            (root / 'tools/regression_sources.json').write_text(json.dumps({'adc':fixture}))
            with patch.object(historical.subprocess,'run') as run, patch.object(historical.subprocess,'check_output',return_value=raw):
                run.return_value.returncode=1
                verified=historical.ensure('adc',root=root)
                self.assertEqual(run.call_args_list[1].args[0],['git','fetch','--no-tags','origin',pins['commit']])
                self.assertEqual(verified['files']['main/adapters/nn2_ads131_esp.c'],expected)
            with patch.object(historical.subprocess,'run') as run:
                run.return_value.returncode=1
                with self.assertRaisesRegex(RuntimeError,'unavailable'):
                    historical.ensure('adc',root=root,verify_only=True)
                self.assertEqual(run.call_count,1)
