#!/usr/bin/env python3
"""Canonical layer schema and detection; not an include/dependency scanner.

Schema v2 keeps the three inner rows (core, ports, app). Optional outer_layers
add adapter (nn2_ad_*), platform (nn2_rtos), and composition (main). A manifest
without outer_layers still loads.
"""
import argparse
import json
import os
import re
from pathlib import Path
import sys

MANIFEST = Path(__file__).with_name('layers.json')
INNER_KEYS = {'schema_version', 'layers', 'source_extensions', 'forbidden_include_regex'}
OUTER_KEY = 'outer_layers'
ADAPTER_PATTERN = 'nn2_ad_*'
ADAPTER_COMPONENT = re.compile(r'nn2_ad_[a-z][a-z0-9_]*\Z')

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key: ' + key)
        result[key] = value
    return result

def match_adapter_component(name):
    return ADAPTER_COMPONENT.fullmatch(name) is not None

def _string_list(values, label, pattern=None):
    if (not isinstance(values, list) or any(not isinstance(x, str) or not x for x in values)
            or len(set(values)) != len(values)):
        raise ValueError('invalid or duplicate ' + label)
    if pattern is not None:
        for value in values:
            if not re.fullmatch(pattern, value):
                raise ValueError('invalid ' + label)
    return values

def _regex_list(values, label):
    _string_list(values, label)
    for pattern in values:
        try:
            re.compile(pattern)
        except re.error as exc:
            raise ValueError('invalid ' + label) from exc
    return values

def _require_bool(row, key):
    if type(row[key]) is not bool:
        raise ValueError('invalid ' + key)
    return row[key]

def _prefixes(values, seen):
    _string_list(values, 'header prefixes', r'nn2_[a-z0-9_]+_')
    if not values:
        raise ValueError('invalid header prefixes')
    for value in values:
        if any(value.startswith(p) or p.startswith(value) for p in seen):
            raise ValueError('conflicting header prefixes')
        seen.add(value)
    return values

def validate_outer_layers(data, inner_rows):
    """Pin the Proposed adapter/platform/composition map. Absent means v1 shape."""
    if OUTER_KEY not in data:
        return None
    rows = data[OUTER_KEY]
    if not isinstance(rows, list) or len(rows) != 3:
        raise ValueError('exactly three outer layers required')
    by_name = {}
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError('outer layer must be an object')
        name = row.get('name')
        if name in by_name or name not in {'adapter', 'platform', 'composition'}:
            raise ValueError('unexpected or duplicate outer layer')
        by_name[name] = row
    adapter, platform, composition = by_name['adapter'], by_name['platform'], by_name['composition']
    adapter_keys = {
        'name', 'component_pattern', 'path_pattern', 'header_prefixes',
        'allowed_dependencies', 'forbidden_dependencies', 'forbidden_dependency_patterns',
        'allow_idf_dependencies', 'forbidden_include_prefixes', 'allow_platform_includes',
        'allow_sdkconfig'}
    platform_keys = {
        'name', 'component', 'path', 'header_prefixes', 'allowed_dependencies',
        'forbidden_dependencies', 'forbidden_dependency_patterns', 'allow_idf_dependencies',
        'forbidden_include_prefixes', 'allow_platform_includes', 'allow_sdkconfig'}
    composition_keys = {
        'name', 'component', 'path', 'header_prefixes', 'allow_any_dependency',
        'allow_platform_includes', 'allow_sdkconfig'}
    if set(adapter) != adapter_keys or set(platform) != platform_keys or set(composition) != composition_keys:
        raise ValueError('unexpected outer-layer keys')
    if adapter['component_pattern'] != ADAPTER_PATTERN or adapter['path_pattern'] != 'components/nn2_ad_*':
        raise ValueError('adapter pattern must be nn2_ad_*')
    if set(adapter['allowed_dependencies']) != {'nn2_core', 'nn2_ports', 'nn2_rtos'}:
        raise ValueError('adapter dependencies must stay nn2_ports, nn2_core, and nn2_rtos')
    if 'nn2_app' not in adapter['forbidden_dependencies']:
        raise ValueError('adapter must forbid nn2_app')
    _string_list(adapter['allowed_dependencies'], 'allowed dependencies', r'nn2_[a-z0-9_]+')
    _string_list(adapter['forbidden_dependencies'], 'forbidden dependencies', r'nn2_[a-z0-9_]+')
    _regex_list(adapter['forbidden_dependency_patterns'], 'forbidden dependency pattern')
    if not any(re.fullmatch(p, 'nn2_ad_x') for p in adapter['forbidden_dependency_patterns']):
        raise ValueError('adapter must forbid adapter-to-adapter dependencies')
    for dep in adapter['allowed_dependencies']:
        if dep in adapter['forbidden_dependencies'] or any(re.fullmatch(p, dep) for p in adapter['forbidden_dependency_patterns']):
            raise ValueError('adapter forbid list cancels an allowed dependency')
    if not _require_bool(adapter, 'allow_idf_dependencies') or not _require_bool(adapter, 'allow_platform_includes') or not _require_bool(adapter, 'allow_sdkconfig'):
        raise ValueError('adapter platform includes and IDF dependencies must stay allowed')
    if platform['component'] != 'nn2_rtos' or platform['path'] != 'components/nn2_rtos':
        raise ValueError('platform row must be components/nn2_rtos')
    if platform['allowed_dependencies'] != [] or platform['forbidden_dependencies'] != []:
        raise ValueError('nn2_rtos must not depend on any nn2_* component')
    _regex_list(platform['forbidden_dependency_patterns'], 'forbidden dependency pattern')
    if not any(re.fullmatch(p, 'nn2_core') and re.fullmatch(p, 'nn2_ad_x') and re.fullmatch(p, 'nn2_rtos')
               for p in platform['forbidden_dependency_patterns']):
        raise ValueError('nn2_rtos must forbid every nn2_* dependency')
    if not _require_bool(platform, 'allow_idf_dependencies') or not _require_bool(platform, 'allow_platform_includes') or not _require_bool(platform, 'allow_sdkconfig'):
        raise ValueError('nn2_rtos platform includes must stay allowed')
    if composition['component'] != 'main' or composition['path'] != 'main':
        raise ValueError('composition row must be main')
    if not _require_bool(composition, 'allow_any_dependency') or not _require_bool(composition, 'allow_platform_includes') or not _require_bool(composition, 'allow_sdkconfig'):
        raise ValueError('composition must be allowed to link every component')
    seen = set()
    for row in inner_rows:
        _prefixes(row['header_prefixes'], seen)
    for row in (adapter, platform, composition):
        _prefixes(row['header_prefixes'], seen)
        _string_list(row.get('forbidden_include_prefixes', []), 'forbidden include prefixes', r'nn2_[a-z0-9_]*')
    if platform['header_prefixes'] != ['nn2_rtos_'] or adapter['header_prefixes'] != ['nn2_ad_']:
        raise ValueError('unexpected outer header prefix')
    if 'nn2_' not in platform['forbidden_include_prefixes']:
        raise ValueError('nn2_rtos must forbid nn2_* includes')
    inner_paths = {row['path'] for row in inner_rows}
    if platform['path'] in inner_paths or composition['path'] in inner_paths:
        raise ValueError('outer path collides with an inner layer')
    return [adapter, platform, composition]

def load_contract(path=MANIFEST):
    data = json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    if not isinstance(data, dict) or not INNER_KEYS <= set(data) <= INNER_KEYS | {OUTER_KEY}:
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
    validate_outer_layers(data, rows)
    return data

def load_manifest(path=MANIFEST):
    return load_contract(path)['layers']

def linked(path):
    return path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction())

def mapped_component(name, rows, outer):
    if any(row['path'] == 'components/' + name for row in rows):
        return True
    for row in outer or []:
        if row.get('component') == name:
            return True
        if row.get('component_pattern') == ADAPTER_PATTERN and match_adapter_component(name):
            return True
    return False

def detect(root, rows, outer=None):
    root = Path(root)
    if not root.is_dir():
        raise ValueError('repository root missing')
    base = root / 'components'
    if linked(base) or (base.exists() and not base.is_dir()):
        raise ValueError('components must be a real directory')
    paths = [row['path'] for row in rows]
    outer = outer or []
    outer_present = []
    if base.exists():
        with os.scandir(base) as entries:
            for entry in sorted(entries, key=lambda x: x.name):
                if not entry.name.startswith('nn2_'):
                    continue
                rel = 'components/' + entry.name
                if not mapped_component(entry.name, rows, outer):
                    raise ValueError('unmapped component: ' + entry.name)
                if rel in paths:
                    continue
                found = Path(entry.path)
                if linked(found) or not entry.is_dir(follow_symlinks=False):
                    raise ValueError('layer must be a real directory: ' + rel)
                outer_present.append(rel)
    present = []
    for path in paths:
        d = root / path
        if linked(d) or (d.exists() and not d.is_dir()):
            raise ValueError('layer must be a real directory: ' + path)
        if d.is_dir():
            present.append(path)
    for row in outer:
        if row.get('path') in paths or row.get('path') in outer_present:
            continue
        if 'path' not in row:
            continue
        d = root / row['path']
        if not d.exists():
            continue
        if linked(d) or not d.is_dir():
            raise ValueError('layer must be a real directory: ' + row['path'])
        outer_present.append(row['path'])
    return present + outer_present

def main():
    # Shell/CMake consumers need LF even with native Windows Python.
    sys.stdout.reconfigure(newline="\n")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['paths', 'detect'])
    parser.add_argument('--root', type=Path, default=Path('.'))
    parser.add_argument('--manifest', type=Path, default=MANIFEST)
    args = parser.parse_args()
    try:
        policy = load_contract(args.manifest)
        rows = policy['layers']
        if args.action == 'paths':
            # C11 enforcement stays on the inner rows. Outer rows keep the SDK default.
            print('\n'.join(row['path'] for row in rows))
        else:
            paths = detect(args.root, rows, policy.get(OUTER_KEY))
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
