#!/usr/bin/env python3
"""Fingerprint active implementation/config/test inputs, without building anything."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PREFIXES = ('main/', 'components/', 'bootloader_components/', 'cmake/', 'tools/', 'test/', 'tests/')
ROOT_FILES = {'CMakeLists.txt', 'sdkconfig.defaults', 'sdkconfig.communication',
              'sdkconfig.ads131-diagnostic', 'sdkconfig.led-bringup', 'sdkconfig.led-short-bench', 'VERSION', 'reference/MANIFEST.sha256'}


def main():
    raw = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT)
    paths = sorted(set(p.decode('utf-8') for p in raw.split(b'\0') if p))
    entries = []
    for name in paths:
        path = ROOT / name
        if path.is_file() and (name in ROOT_FILES or name.startswith(PREFIXES)):
            entries.append({'path': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    inventory = ''.join(e['sha256'] + '  ' + e['path'] + '\n' for e in entries).encode()
    record = {
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'branch': subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
        'implementation_sha256': hashlib.sha256(inventory).hexdigest(),
        'scope': 'Active implementation/config/tools/tests plus reference manifest; excludes docs, editor state, generated sdkconfig/build and logs',
        'files': entries,
    }
    destination = ROOT / 'docs/evidence/communication-source.json'
    destination.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(record['implementation_sha256'])


if __name__ == '__main__':
    main()
