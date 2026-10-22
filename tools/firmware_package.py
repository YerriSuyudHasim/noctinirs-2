#!/usr/bin/env python3
"""Portable board-v1 packages. Validation only reads ordinary files; never opens devices.

Checksums detect accidental changes, not authenticity: verify the externally supplied
ZIP SHA256 and expected source/profile/config/tool identities before trusting a package.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import stat
import subprocess
import sys
import zipfile

SCHEMA = 1
BOARD = 'Nocti board v1'
MODULE = 'ESP32-S3-WROOM-1U-N16R8'
CONTAINER = 'espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c'
IDF = 'fff9895c82d744c7237be8847347bdd1b07c6643'
LABELS = {'fake': 'FAKE / LED DISABLED', 'diagnostic': 'REAL ADC DIAGNOSTIC / EMITTER INACTIVE'}
ARGS = ('flash_args', 'flash_project_args', 'flash_app_args', 'flash_bootloader_args')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def unique(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, 'duplicate JSON key: ' + key)
        out[key] = value
    return out


def loads(raw):
    return json.loads(raw, object_pairs_hook=unique)


def dumps(obj):
    return (json.dumps(obj, indent=2, sort_keys=True) + '\n').encode()


def safe(name):
    require(isinstance(name, str) and bool(name), 'empty path')
    require(re.fullmatch(r'[A-Za-z0-9._ /-]+', name) is not None, 'unsafe/nonportable path characters')
    require(not any(c in name for c in '\\:\x00\r\n'), 'unsafe path: ' + repr(name))
    p = PurePosixPath(name)
    require(not p.is_absolute() and all(x not in ('', '.', '..') for x in name.split('/')),
            'unsafe path: ' + name)
    require(all(not x.endswith((' ', '.')) and x.split('.')[0].upper() not in
                {'CON', 'NUL', 'PRN', 'AUX', *('COM'+str(i) for i in range(10)),
                 *('LPT'+str(i) for i in range(10))} for x in p.parts), 'nonportable path')
    require(not any(c in name for c in '<>"|?*'), 'nonportable path')
    return name


def file_bytes(root, name):
    safe(name)
    path = root
    for part in PurePosixPath(name).parts:
        path = path / part
        require(not path.is_symlink() and not path.is_junction(), 'symlink/junction: ' + name)
    require(path.is_file(), 'missing ordinary file: ' + name)
    return path.read_bytes()


def inventory(path):
    out = {}
    require(not path.is_symlink() and not path.is_junction(), 'symlink/junction package')
    if path.is_file():
        with zipfile.ZipFile(path) as z:
            names = set()
            for e in z.infolist():
                name = e.filename.rstrip('/') if e.is_dir() else e.filename
                safe(name)
                require(name.casefold() not in names, 'duplicate/ambiguous ZIP entry')
                names.add(name.casefold())
                mode = e.external_attr >> 16
                require(stat.S_IFMT(mode) in (0, stat.S_IFREG, stat.S_IFDIR), 'archive symlink/special entry')
                require(e.file_size <= 32*1024*1024, 'oversize archive member')
                if not e.is_dir():
                    out[name] = z.read(e)
    else:
        require(path.is_dir() and not path.is_symlink() and not path.is_junction(), 'invalid package directory')
        for p in path.rglob('*'):
            require(not p.is_symlink() and not p.is_junction(), 'symlink/junction entry')
            if p.is_file():
                name = safe(p.relative_to(path).as_posix())
                require(name.casefold() not in {x.casefold() for x in out}, 'ambiguous package path')
                out[name] = file_bytes(path, name)
    return out


def config(raw):
    out = {}
    for line in raw.decode().splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            k, v = line.split('=', 1)
            # IDF6.1 emits repeated compatibility assignments; identical values are unambiguous.
            value = v.strip('"')
            require(k not in out or out[k] == value, 'conflicting sdkconfig assignment')
            out[k] = value
    return out


def settings(raw, profile):
    c = config(raw)
    require(c.get('CONFIG_IDF_TARGET') == 'esp32s3', 'wrong target config')
    required = 'CONFIG_NN2_PROFILE_' + ('FAKE' if profile == 'fake' else 'ADS131_DIAGNOSTIC')
    other = 'CONFIG_NN2_PROFILE_' + ('ADS131_DIAGNOSTIC' if profile == 'fake' else 'FAKE')
    require(c.get(required) == 'y' and c.get(other, 'n') == 'n', 'wrong profile config')
    for k in ('SPIRAM', 'SPIRAM_MODE_OCT', 'ESPTOOLPY_OCT_FLASH', 'SECURE_BOOT', 'FLASH_ENCRYPTION_ENABLED'):
        require(c.get('CONFIG_' + k, 'n') == 'n', 'prohibited config ' + k)
    size = c.get('CONFIG_ESPTOOLPY_FLASHSIZE')
    require(size == ('2MB' if profile == 'fake' else '16MB'), 'wrong profile capacity')
    flash = {k: c.get('CONFIG_ESPTOOLPY_' + k.upper()) for k in ('flashmode', 'flashfreq', 'flashsize')}
    require(flash['flashmode'] == 'dio' and flash['flashfreq'] == '80m', 'wrong flash settings')
    if profile == 'fake':
        require(c.get('CONFIG_ESP_CONSOLE_UART_DEFAULT') == 'y' and
                c.get('CONFIG_ESP_CONSOLE_UART_NUM') == '0' and
                c.get('CONFIG_ESP_CONSOLE_UART_BAUDRATE') == '115200', 'wrong UART config')
        console = {'primary': 'UART0', 'baud': 115200, 'tx_gpio': 43, 'rx_gpio': 44,
                   'pin_basis': 'ESP32-S3 UART0 default IOMUX; board routing requires Hardware review',
                   'secondary': 'USB Serial/JTAG' if c.get('CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG') == 'y' else 'none'}
    else:
        require(c.get('CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG') == 'y' and
                c.get('CONFIG_ESP_CONSOLE_UART_NUM') == '-1', 'wrong USB config')
        console = {'primary': 'USB Serial/JTAG', 'dm_gpio': 19, 'dp_gpio': 20,
                   'baud': None, 'secondary': 'none', 'pin_basis': 'ESP32-S3 fixed USB pins; board USB routing pending fixture review'}
    return {'flash_mode': flash['flashmode'], 'flash_freq': flash['flashfreq'], 'flash_size': size}, console


def address(value):
    require(isinstance(value, str) and re.fullmatch(r'0x[0-9a-fA-F]+', value), 'invalid address')
    return int(value, 16)


def parse_args(raw):
    # Lexical parsing only; no subprocess, eval, shell expansion, or device access.
    tokens = shlex.split(raw.decode(), comments=False, posix=True)
    opts, images = {}, []
    while tokens and tokens[0].startswith('--'):
        key = tokens.pop(0)
        require(key in ('--flash-mode', '--flash-freq', '--flash-size') and key not in opts and tokens,
                'unexpected/duplicate flash argument')
        opts[key] = tokens.pop(0)
    require(len(tokens) % 2 == 0, 'truncated flash arguments')
    while tokens:
        off, name = tokens[:2]
        tokens = tokens[2:]
        images.append((address(off), safe(name)))
    require(len({x[0] for x in images}) == len(images), 'duplicate flash argument address')
    return opts, images


def derive(files, profile):
    require(profile in LABELS, 'unknown profile')
    f = loads(files['metadata/flasher_args.json'])
    require(set(f) == {'write_flash_args', 'flash_settings', 'flash_files', 'bootloader',
                       'partition-table', 'app', 'extra_esptool_args'}, 'unexpected flash metadata fields')
    flash, console = settings(files['metadata/sdkconfig'], profile)
    require(f['flash_settings'] == flash, 'config/flash metadata mismatch')
    extra = f['extra_esptool_args']
    require(extra == {'chip': 'esp32s3', 'before': 'default-reset', 'after': 'hard-reset', 'stub': True},
            'unexpected chip/reset/security arguments')
    options = {'--flash-mode': flash['flash_mode'], '--flash-size': flash['flash_size'], '--flash-freq': flash['flash_freq']}
    opts, no_images = parse_args(' '.join(f['write_flash_args']).encode())
    require(opts == options and not no_images, 'inconsistent write_flash_args')
    images = []
    end = 0
    paths = set()
    for off, name in sorted(f['flash_files'].items(), key=lambda x: address(x[0])):
        addr = address(off)
        name = safe(name)
        require(name.casefold() not in paths, 'duplicate image path')
        paths.add(name.casefold())
        package = 'images/' + name
        raw = files[package]
        require(len(raw) > 0, 'empty image')
        require(addr >= end, 'duplicate/overlapping flash ranges')
        end = addr + len(raw)
        require(end <= int(flash['flash_size'][:-2])*1024*1024, 'flash capacity exceeded')
        images.append({'original_path': name, 'package_path': package, 'address': addr,
                       'address_hex': off, 'size': len(raw), 'sha256': digest(raw)})
    require(bool(images), 'empty flash map')
    pairs = [(x['address'], x['original_path']) for x in images]
    roles = {}
    for role in ('bootloader', 'partition-table', 'app'):
        row = f[role]
        require(set(row) == {'offset', 'file', 'encrypted'} and row['encrypted'] == 'false', 'unsupported role metadata')
        require((address(row['offset']), safe(row['file'])) in pairs, 'missing required ' + role + ' image')
        roles[role] = (address(row['offset']), row['file'])
    require(len(set(roles.values())) == 3, 'ambiguous required images')
    # These bytes identify the actual ESP images and partition table, not a compiler ABI file.
    for role in ('bootloader', 'app'):
        require(files['images/' + roles[role][1]][:1] == b'\xe9', 'invalid ESP image header')
    require(files['images/' + roles['partition-table'][1]][:2] == b'\xaa\x50', 'invalid partition table header')
    for arg in ARGS:
        opts, refs = parse_args(files['metadata/' + arg])
        expected = pairs if arg in ('flash_args', 'flash_project_args') else [roles['app' if arg == 'flash_app_args' else 'bootloader']]
        require(opts == options and refs == expected, 'inconsistent ' + arg)
    return images, flash, console, extra


def validate(path, expect_sha=None, expect_profile=None, expect_config=None, expect_tool=None):
    files = inventory(Path(path))
    m = loads(files['manifest.json'])
    require(m['schema_version'] == SCHEMA and m['board'] == BOARD and m['module'] == MODULE, 'wrong schema/board/module')
    profile = m['profile']
    images, flash, console, extra = derive(files, profile)
    require(m['label'] == LABELS[profile] and m['images'] == images and m['flash_settings'] == flash
            and m['console'] == console and m['extra_esptool_args'] == extra, 'stale/inconsistent manifest')
    p = loads(files['metadata/provenance.json'])
    require(m['provenance'] == p, 'stale source provenance')
    for k in ('firmware_source_sha', 'firmware_tree', 'packaging_tool_sha'):
        require(re.fullmatch('[0-9a-f]{40}', p[k]) is not None, 'invalid source identity')
    require(p['firmware_clean'] is True and p['packaging_tool_clean'] is True, 'unclean source')
    require(p['container'] == CONTAINER and p['idf_commit'] == IDF, 'wrong build environment')
    require(p['packaging_tool_file_sha256'] == digest(files['validate_package.py']), 'wrong validator/tool identity')
    require(p['sdkconfig_sha256'] == digest(files['metadata/sdkconfig']), 'wrong config identity')
    tc = files['metadata/toolchain.txt'].decode()
    for text in ('SOURCE_SHA='+p['firmware_source_sha'], 'IDF commit: '+IDF,
                 'GITHUB_RUN_ID='+str(p['ci']['run_id'])+' attempt='+str(p['ci']['attempt']), 'EVENT='+p['ci']['event']):
        require(text in tc.splitlines(), 'source/run/toolchain mismatch: ' + text)
    for name, h in p['defaults_sha256'].items():
        require(digest(files['metadata/defaults/'+safe(name)]) == h, 'wrong defaults')
    require(set(p['defaults_sha256']) == ({'sdkconfig.defaults'} if profile == 'fake' else
                                        {'sdkconfig.defaults', 'sdkconfig.ads131-diagnostic'}), 'wrong defaults profile')
    for got, expected, label in ((p['firmware_source_sha'], expect_sha, 'source'), (profile, expect_profile, 'profile'),
                                 (p['sdkconfig_sha256'], expect_config, 'config'), (p['packaging_tool_sha'], expect_tool, 'tool')):
        require(expected is None or got == expected, 'wrong expected ' + label)
    expected_files = {'manifest.json', 'SHA256SUMS', 'validate_package.py', 'INSTALL_RECOVERY_REVIEW.md',
                      'metadata/provenance.json', 'metadata/sdkconfig', 'metadata/toolchain.txt', 'metadata/flasher_args.json',
                      *('metadata/'+a for a in ARGS), *('metadata/defaults/'+n for n in p['defaults_sha256']),
                      *(x['package_path'] for x in images)}
    require(set(files) == expected_files, 'missing/unexpected package files')
    checks = {}
    for line in files['SHA256SUMS'].decode().splitlines():
        require(re.fullmatch(r'[0-9a-f]{64}  .+', line) is not None, 'invalid checksum line')
        h, name = line.split('  ', 1)
        safe(name)
        require(name not in checks, 'duplicate checksum entry')
        checks[name] = h
    require(set(checks) == set(files)-{'SHA256SUMS'}, 'incomplete checksum inventory')
    for name, h in checks.items():
        require(digest(files[name]) == h, 'checksum mismatch: ' + name)
    return m


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


def create(build, evidence, repo, firmware_sha, profile, ci, output, input_artifact=None):
    repo, build, evidence = Path(repo), Path(build), Path(evidence)
    tool = git(repo, 'rev-parse', 'HEAD').decode().strip()
    require(git(repo, 'show', 'HEAD:tools/firmware_package.py') == Path(__file__).read_bytes(), 'tool bytes differ from committed source')
    require(not git(repo, 'status', '--porcelain', '--untracked-files=no').strip(), 'packaging tooling is dirty')
    files = {'metadata/flasher_args.json': file_bytes(build, 'flasher_args.json'),
             'metadata/sdkconfig': file_bytes(evidence, 'sdkconfig'),
             'metadata/toolchain.txt': file_bytes(evidence, 'toolchain.txt')}
    for arg in ARGS:
        files['metadata/'+arg] = file_bytes(build, arg)
    meta = loads(files['metadata/flasher_args.json'])
    for name in meta['flash_files'].values():
        files['images/'+safe(name)] = file_bytes(build, name)
    images, flash, console, extra = derive(files, profile)
    defaults = ['sdkconfig.defaults'] + (['sdkconfig.ads131-diagnostic'] if profile == 'diagnostic' else [])
    for name in defaults:
        files['metadata/defaults/'+name] = git(repo, 'show', firmware_sha+':'+name)
    p = {'firmware_source_sha': firmware_sha, 'firmware_tree': git(repo, 'rev-parse', firmware_sha+'^{tree}').decode().strip(),
         'firmware_clean': True, 'firmware_clean_basis': 'CI exact source checkout; retained source-bound build evidence',
         'packaging_tool_sha': tool, 'packaging_tool_clean': True,
         'packaging_tool_file_sha256': digest(Path(__file__).read_bytes()),
         'sdkconfig_sha256': digest(files['metadata/sdkconfig']),
         'defaults_sha256': {n: digest(files['metadata/defaults/'+n]) for n in defaults},
         'container': CONTAINER, 'idf_commit': IDF, 'toolchain': files['metadata/toolchain.txt'].decode(),
         'ci': ci, 'input_artifact': input_artifact,
         'created_at_utc': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
         'physical_evidence': 'NOT RUN'}
    files['metadata/provenance.json'] = dumps(p)
    m = {'schema_version': SCHEMA, 'board': BOARD, 'module': MODULE, 'profile': profile, 'label': LABELS[profile],
         'images': images, 'flash_settings': flash, 'console': console, 'extra_esptool_args': extra, 'provenance': p,
         'metadata_locator_classification': {'IDF_PATH': 'non-file build environment locator; not a payload reference'},
         'checksum_scope': 'SHA256SUMS covers every ordinary package file except itself; ZIP SHA256 is published externally; no signing/authenticity claim'}
    files['manifest.json'] = dumps(m)
    files['validate_package.py'] = Path(__file__).read_bytes()
    guide = (repo/'docs/BOARD_V1_PACKAGE_GUIDE.md').read_text(encoding='utf-8')
    preview = ['python -m esptool', '--chip '+extra['chip'], '--port <HARDWARE_REVIEWED_PORT>',
               '--before '+extra['before'], '--after '+extra['after'], 'write-flash',
               '--flash-mode '+flash['flash_mode'], '--flash-freq '+flash['flash_freq'], '--flash-size '+flash['flash_size']]
    preview += [x['address_hex']+' "'+x['package_path']+'"' for x in images]
    guide += '\n## This package (generated)\n\n'+LABELS[profile]+'\n\nFirmware SHA: `'+firmware_sha+'`\n\nTool SHA: `'+tool+'`\n\nConsole (generated config):\n\n```json\n'+json.dumps(console, indent=2)+'\n```\n\nCommand PREVIEW ONLY, from extracted package root, after validation and separate physical approval:\n\n```text\n'+' '.join(preview)+'\n```\n\nGenerated reset arguments describe requested tool behavior, not verified board reset wiring. No erase-all or security changes are authorized.\n'
    files['INSTALL_RECOVERY_REVIEW.md'] = guide.encode()
    files['SHA256SUMS'] = ''.join(digest(v)+'  '+n+'\n' for n, v in sorted(files.items())).encode()
    output = Path(output)
    require(not output.exists(), 'refusing to overwrite package')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for name, raw in sorted(files.items()):
            z.writestr(name, raw)
    validate(output, firmware_sha, profile, p['sdkconfig_sha256'], tool)
    return m


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    val = sub.add_parser('validate', help='offline read-only validation, no SDK/device required')
    val.add_argument('package', type=Path)
    for name in ('sha', 'profile', 'config', 'tool'):
        val.add_argument('--expect-'+name)
    gen = sub.add_parser('create')
    for name in ('build', 'evidence', 'repo', 'firmware-sha', 'profile', 'ci-json', 'output'):
        gen.add_argument('--'+name, required=True)
    gen.add_argument('--input-artifact-json')
    args = parser.parse_args()
    try:
        if args.command == 'validate':
            m = validate(args.package, args.expect_sha, args.expect_profile, args.expect_config, args.expect_tool)
        else:
            m = create(args.build, args.evidence, args.repo, args.firmware_sha, args.profile,
                       loads(Path(args.ci_json).read_bytes()), args.output,
                       loads(Path(args.input_artifact_json).read_bytes()) if args.input_artifact_json else None)
        print('PASS:', m['label'], 'firmware='+m['provenance']['firmware_source_sha'], 'images='+str(len(m['images'])))
        return 0
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile, RuntimeError) as exc:
        print('FAIL:', str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
