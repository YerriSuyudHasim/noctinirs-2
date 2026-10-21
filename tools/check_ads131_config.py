#!/usr/bin/env python3
"""Validate generated diagnostic config; selftest preserves meaningful negatives."""
import argparse
from pathlib import Path
import tempfile


def check(path, diagnostic):
    text = path.read_text()
    config = dict(line.split('=', 1) for line in text.splitlines()
                  if line.startswith('CONFIG_') and '=' in line)
    if config.get('CONFIG_IDF_TARGET') != '"esp32s3"':
        raise ValueError('wrong generated target')
    if diagnostic:
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
    elif config.get('CONFIG_NN2_PROFILE_FAKE') != 'y':
        raise ValueError('default fake profile missing')


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


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', nargs='?', type=Path)
    parser.add_argument('--diagnostic', action='store_true')
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args()
    if args.selftest:
        selftest()
    elif args.config:
        check(args.config, args.diagnostic)
        print('PASS generated profile config')
    else:
        parser.error('config or --selftest required')
