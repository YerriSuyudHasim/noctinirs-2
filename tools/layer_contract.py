#!/usr/bin/env python3
"""Canonical layer schema and detection; not an include/dependency scanner."""
import argparse
import json
import os
from pathlib import Path
import sys

MANIFEST = Path(__file__).with_name('layers.json')

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key: ' + key)
        result[key] = value
    return result

def load_manifest(path=MANIFEST):
    data = json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    if not isinstance(data, dict) or set(data) != {'schema_version', 'layers'}:
        raise ValueError('expected schema_version and layers only')
    if type(data['schema_version']) is not int or data['schema_version'] != 1:
        raise ValueError('unsupported schema version')
    rows = data['layers']
    if not isinstance(rows, list) or len(rows) != 3:
        raise ValueError('exactly three inner layers required')
    names = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {'name', 'path'}:
            raise ValueError('layer requires name and path only')
        name = row['name']
        if not isinstance(name, str) or name not in {'core', 'ports', 'app'} or name in names:
            raise ValueError('unexpected or duplicate layer name')
        if row['path'] != 'components/nn2_' + name:
            raise ValueError('unexpected layer path')
        names.add(name)
    return rows

def linked(path):
    return path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction())

def detect(root, rows):
    root = Path(root)
    if not root.is_dir():
        raise ValueError('repository root missing')
    base = root / 'components'
    if linked(base) or (base.exists() and not base.is_dir()):
        raise ValueError('components must be a real directory')
    paths = [row['path'] for row in rows]
    if base.exists():
        with os.scandir(base) as entries:
            for entry in entries:
                if entry.name.startswith('nn2_') and 'components/' + entry.name not in paths:
                    raise ValueError('unmapped component: ' + entry.name)
    present = []
    for path in paths:
        d = root / path
        if linked(d) or (d.exists() and not d.is_dir()):
            raise ValueError('layer must be a real directory: ' + path)
        if d.is_dir():
            present.append(path)
    return present

def main():
    # Shell/CMake consumers need LF even with native Windows Python.
    sys.stdout.reconfigure(newline="\n")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['paths', 'detect'])
    parser.add_argument('--root', type=Path, default=Path('.'))
    parser.add_argument('--manifest', type=Path, default=MANIFEST)
    args = parser.parse_args()
    try:
        rows = load_manifest(args.manifest)
        if args.action == 'paths':
            print('\n'.join(row['path'] for row in rows))
        else:
            paths = detect(args.root, rows)
            print('present=' + ('true' if paths else 'false'))
            print('dirs=' + ' '.join(paths))
            if not paths:
                print('NOT APPLICABLE (not evidence): no normative layers', file=sys.stderr)
    except (OSError, ValueError, TypeError) as exc:
        print('FAIL: layer contract: ' + str(exc), file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
