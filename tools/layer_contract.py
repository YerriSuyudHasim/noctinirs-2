#!/usr/bin/env python3
"""Canonical layer schema and detection; not an include/dependency scanner."""
import argparse
import json
import os
import re
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

def load_contract(path=MANIFEST):
    data = json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    if not isinstance(data, dict) or set(data) != {'schema_version', 'layers', 'source_extensions', 'forbidden_include_regex'}:
        raise ValueError('expected schema_version, layers, source_extensions and forbidden_include_regex only')
    if type(data['schema_version']) is not int or data['schema_version'] != 2:
        raise ValueError('unsupported schema version')
    rows = data['layers']
    if not isinstance(rows, list) or len(rows) != 3:
        raise ValueError('exactly three inner layers required')
    names = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {'name', 'path', 'requires', 'header_prefixes'}:
            raise ValueError('layer requires name, path, requires and header_prefixes only')
        name = row['name']
        if not isinstance(name, str) or name not in {'core', 'ports', 'app'} or name in names:
            raise ValueError('unexpected or duplicate layer name')
        if row['path'] != 'components/nn2_' + name:
            raise ValueError('unexpected layer path')
        names.add(name)
    components = {Path(row['path']).name for row in rows}
    prefixes = set()
    for row in rows:
        deps = row['requires']
        if (not isinstance(deps, list) or any(not isinstance(x, str) for x in deps)
                or len(set(deps)) != len(deps) or not set(deps) <= components
                or Path(row['path']).name in deps):
            raise ValueError('invalid, duplicate, unknown or self dependency')
        values = row['header_prefixes']
        if (not isinstance(values, list) or not values
                or any(not isinstance(x, str) or not re.fullmatch(r'nn2_[a-z0-9_]+_', x) for x in values)
                or len(set(values)) != len(values)):
            raise ValueError('invalid header prefixes')
        for value in values:
            if any(value.startswith(p) or p.startswith(value) for p in prefixes):
                raise ValueError('conflicting header prefixes')
            prefixes.add(value)
    graph = {Path(r['path']).name: r['requires'] for r in rows}
    def visit(name, active):
        if name in active:
            raise ValueError('cyclic layer dependencies')
        for dep in graph[name]:
            visit(dep, active | {name})
    for name in graph:
        visit(name, set())
    for field in ('source_extensions', 'forbidden_include_regex'):
        values = data[field]
        if (not isinstance(values, list) or not values
                or any(not isinstance(x, str) or not x for x in values)
                or len(set(values)) != len(values)):
            raise ValueError('invalid or empty ' + field)
    if any(not re.fullmatch(r'\.[a-z+]+', x) for x in data['source_extensions']):
        raise ValueError('invalid source extension')
    for pattern in data['forbidden_include_regex']:
        try:
            re.compile(pattern)
        except re.error as exc:
            raise ValueError('invalid forbidden include expression') from exc
    return data

def load_manifest(path=MANIFEST):
    return load_contract(path)['layers']

def linked(path):
    return path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction())

def platform_paths(root, inner_paths):
    """Explicit platform inventory; absence still rejects unknown components."""
    manifest = Path(root) / 'tools/platform_components.json'
    if linked(manifest):
        raise ValueError('platform manifest must be a real file')
    if not manifest.exists():
        return []
    data = json.loads(manifest.read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    if (not isinstance(data, dict) or set(data) != {'schema_version', 'paths'}
            or type(data['schema_version']) is not int or data['schema_version'] != 1):
        raise ValueError('invalid platform manifest schema')
    paths = data['paths']
    if (not isinstance(paths, list) or not paths
            or any(not isinstance(p, str) or not re.fullmatch(r'components/nn2_[a-z0-9_]+', p) for p in paths)
            or len(set(paths)) != len(paths) or set(paths).intersection(inner_paths)):
        raise ValueError('invalid, duplicate or inner-layer platform path')
    for path in paths:
        directory = Path(root) / path
        if linked(directory) or not directory.is_dir():
            raise ValueError('platform component must be a real directory: ' + path)
    return paths


def detect(root, rows):
    root = Path(root)
    if not root.is_dir():
        raise ValueError('repository root missing')
    base = root / 'components'
    if linked(base) or (base.exists() and not base.is_dir()):
        raise ValueError('components must be a real directory')
    paths = [row['path'] for row in rows]
    platforms = platform_paths(root, paths)
    if base.exists():
        with os.scandir(base) as entries:
            for entry in sorted(entries, key=lambda x: x.name):
                if entry.name.startswith('nn2_') and 'components/' + entry.name not in paths + platforms:
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
