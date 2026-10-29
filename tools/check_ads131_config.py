#!/usr/bin/env python3
"""Validate generated diagnostic config; selftest preserves meaningful negatives."""
import argparse
from pathlib import Path
import tempfile
import subprocess


def compile_guards(compiler, generated):
    root = Path(__file__).resolve().parents[1]
    header = root / 'main/adapters/nn2_ads131_build_guard.h'
    original = generated.read_text()
    with tempfile.TemporaryDirectory() as tmp:
        dest = Path(tmp)
        source = dest / 'probe.c'
        source.write_text('#include "nn2_ads131_build_guard.h"\nint probe(void) { return 0; }\n')
        cases = [(None, None)] + [(key, '1') for key in
                 ('CONFIG_SPIRAM', 'CONFIG_ESPTOOLPY_OCT_FLASH', 'CONFIG_SPIRAM_MODE_OCT')]
        if '#define CONFIG_NN2_OPTICAL_ACQUISITION 1' in original:
            cases += [(key, '0') for key in ('CONFIG_ESPTOOLPY_FLASHSIZE_16MB', 'CONFIG_PARTITION_TABLE_CUSTOM')]
        for option, value in cases:
            config = original
            if option:
                config += '\n#undef ' + option + '\n#define ' + option + ' ' + value + '\n'
            (dest / 'sdkconfig.h').write_text(config)
            command = [compiler, '-fsyntax-only', '-x', 'c', '-I', str(dest),
                       '-I', str(header.parent), str(source)]
            result = subprocess.run(command, capture_output=True, text=True)
            print('COMMAND', command, 'EXIT', result.returncode)
            print(result.stdout + result.stderr)
            if (result.returncode == 0) != (option is None):
                raise RuntimeError('production compile guard failed: ' + str(option))
            if option and ('ADS131' not in result.stderr or 'requires' not in result.stderr):
                raise RuntimeError('negative failed for unrelated reason')
    print('PASS production compile guard: positive and ' + str(len(cases) - 1) + ' compiler negatives')


def check(path, diagnostic=False, profile=None):
    text = path.read_text()
    config = dict(line.split('=', 1) for line in text.splitlines()
                  if line.startswith('CONFIG_') and '=' in line)
    if config.get('CONFIG_IDF_TARGET') != '"esp32s3"':
        raise ValueError('wrong generated target')
    selected = profile or ('diagnostic' if diagnostic else 'fake')
    choices = {'fake': 'FAKE', 'communication': 'COMMUNICATION', 'diagnostic': 'ADS131_DIAGNOSTIC'}
    enabled = [name for name in choices.values() if config.get('CONFIG_NN2_PROFILE_' + name) == 'y']
    if enabled != [choices['communication' if selected == 'acquisition' else selected]]:
        raise ValueError('expected exactly one profile: ' + selected)
    if selected in ('communication', 'acquisition'):
        for name in ('ESP_CONSOLE_USB_SERIAL_JTAG', 'ESP_WIFI_SOFTAP_SUPPORT'):
            if config.get('CONFIG_' + name) != 'y':
                raise ValueError('required communication option: ' + name)
        if int(config.get('CONFIG_ESP_MAIN_TASK_STACK_SIZE', '0')) < 12288:
            raise ValueError('communication main stack below required12288 bytes')
    if selected == 'acquisition':
        for name in ('NN2_OPTICAL_ACQUISITION', 'NN2_LED_BRINGUP', 'ESP_TASK_WDT_EN', 'ESP_TASK_WDT_INIT', 'ESPTOOLPY_FLASHMODE_DIO', 'ESPTOOLPY_FLASHSIZE_16MB'):
            if config.get('CONFIG_' + name) != 'y':
                raise ValueError('required acquisition option: ' + name)
        for name in ('SPIRAM', 'SPIRAM_MODE_OCT', 'ESPTOOLPY_OCT_FLASH'):
            if config.get('CONFIG_' + name, 'n') != 'n':
                raise ValueError('prohibited acquisition option: ' + name)
        if config.get('CONFIG_NN2_LED_RUNTIME_I2C_HZ') != '800000':
            raise ValueError('acquisition LED runtime must remain800kHz')
        if (config.get('CONFIG_PARTITION_TABLE_CUSTOM') != 'y' or
                config.get('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME') != '"config/partitions/acquisition-16mb.csv"'):
            raise ValueError('acquisition requires the versioned 16 MiB partition table')
    if selected == 'diagnostic':
        for name in ('SPIRAM', 'SPIRAM_MODE_OCT', 'ESPTOOLPY_OCT_FLASH'):
            if config.get('CONFIG_' + name, 'n') != 'n':
                raise ValueError('prohibited generated option: ' + name)
        for name in ('NN2_PROFILE_ADS131_DIAGNOSTIC', 'ESPTOOLPY_FLASHMODE_DIO',
                     'ESPTOOLPY_FLASHSIZE_16MB', 'ESP_CONSOLE_USB_SERIAL_JTAG',
                     'ESP_TASK_WDT_EN', 'ESP_TASK_WDT_INIT'):
            if config.get('CONFIG_' + name) != 'y':
                raise ValueError('required generated option: ' + name)
        if config.get('CONFIG_NN2_PROFILE_FAKE', 'n') != 'n':
            raise ValueError('mutually exclusive profile violation')



def selftest():
    base = 'CONFIG_IDF_TARGET="esp32s3"\n' + '\n'.join('CONFIG_' + n + '=y' for n in (
        'NN2_PROFILE_ADS131_DIAGNOSTIC', 'ESPTOOLPY_FLASHMODE_DIO', 'ESPTOOLPY_FLASHSIZE_16MB',
        'ESP_CONSOLE_USB_SERIAL_JTAG', 'ESP_TASK_WDT_EN', 'ESP_TASK_WDT_INIT')) + '\n'
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / 'sdkconfig'
        p.write_text(base)
        check(p, True)
        for option in ('SPIRAM', 'SPIRAM_MODE_OCT', 'ESPTOOLPY_OCT_FLASH', 'NN2_PROFILE_FAKE'):
            p.write_text(base + 'CONFIG_' + option + '=y\n')
            try:
                check(p, True)
            except ValueError:
                continue
            raise RuntimeError('negative accepted: ' + option)
        for missing in ('ESPTOOLPY_FLASHMODE_DIO', 'ESP_CONSOLE_USB_SERIAL_JTAG', 'ESP_TASK_WDT_INIT'):
            p.write_text(base.replace('CONFIG_' + missing + '=y', ''))
            try:
                check(p, True)
            except ValueError:
                continue
            raise RuntimeError('missing required option accepted: ' + missing)
    print('PASS generated config: positive and 7 negative controls')
    acquisition = ('CONFIG_IDF_TARGET="esp32s3"\n' + '\n'.join('CONFIG_' + name + '=y' for name in (
        'NN2_PROFILE_COMMUNICATION', 'NN2_OPTICAL_ACQUISITION', 'NN2_LED_BRINGUP',
        'ESP_CONSOLE_USB_SERIAL_JTAG', 'ESP_WIFI_SOFTAP_SUPPORT', 'ESP_TASK_WDT_EN',
        'ESP_TASK_WDT_INIT', 'ESPTOOLPY_FLASHMODE_DIO', 'ESPTOOLPY_FLASHSIZE_16MB',
        'PARTITION_TABLE_CUSTOM')) + '\nCONFIG_ESP_MAIN_TASK_STACK_SIZE=12288\n'
        'CONFIG_NN2_LED_RUNTIME_I2C_HZ=800000\n'
        'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="config/partitions/acquisition-16mb.csv"\n')
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / 'sdkconfig'
        p.write_text(acquisition)
        check(p, profile='acquisition')
        for bad in (
                acquisition.replace('CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y', 'CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y'),
                acquisition.replace('CONFIG_PARTITION_TABLE_CUSTOM=y', 'CONFIG_PARTITION_TABLE_SINGLE_APP=y'),
                acquisition.replace('config/partitions/acquisition-16mb.csv', 'partitions_singleapp.csv')):
            p.write_text(bad)
            try:
                check(p, profile='acquisition')
            except ValueError:
                continue
            raise RuntimeError('incorrect acquisition flash/partition profile accepted')
    print('PASS acquisition profile: 16 MiB positive and 3 flash/partition negatives')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', nargs='?', type=Path)
    parser.add_argument('--diagnostic', action='store_true')
    parser.add_argument('--profile', choices=('fake', 'communication', 'diagnostic', 'acquisition'))
    parser.add_argument('--selftest', action='store_true')
    parser.add_argument('--cc', help='compile production guard against generated config/header')
    parser.add_argument('--generated-header', type=Path)
    args = parser.parse_args()
    if args.selftest:
        selftest()
    if args.cc:
        if not args.generated_header:
            parser.error('--cc requires --generated-header')
        compile_guards(args.cc, args.generated_header)
    if args.config:
        if args.diagnostic and args.profile and args.profile != 'diagnostic':
            parser.error('--diagnostic conflicts with --profile')
        check(args.config, args.diagnostic, args.profile)
        print('PASS generated profile config')
    elif not args.selftest and not args.cc:
        parser.error('config or --selftest required')
