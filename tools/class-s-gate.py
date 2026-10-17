#!/usr/bin/env python3
"""ADR-0001 rev1 Class S gate. Exit 0 PASS, 1 FAIL, 3 NOT APPLICABLE."""
import pathlib
import re
import sys

LAYERS = {"nn2_core": set(), "nn2_ports": {"nn2_core"}, "nn2_app": {"nn2_core", "nn2_ports"}}
SOURCE_EXT = {".c", ".h", ".cpp", ".hpp"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
PLATFORM = re.compile(r'^(?:esp_.*|freertos/.*|driver/.*|lwip/.*|hal/.*|soc/.*|rom/.*|esp32s3/rom/.*|xtensa/.*|esp_private/.*|sdkconfig\.h|nimble/.*|host/ble_.*|bt_.*|nvs(?:_.*)?\.h|FreeRTOS\.h|task\.h|queue\.h|semphr\.h|timers\.h|event_groups\.h|nn2_adapters?_.*)$')
CROSS = {"nn2_core": ("nn2_app_", "nn2_port_"), "nn2_ports": ("nn2_app_",), "nn2_app": ()}


def cmake_dependencies(text):
    # Strip line comments, retaining quoted content; fail closed on unsupported dynamic constructs.
    text = re.sub(r'#[^\n]*', '', text)
    calls = list(re.finditer(r'\bidf_component_register\s*\(', text, re.I))
    if len(calls) != 1:
        raise ValueError('expected exactly one idf_component_register(...)')
    pos = calls[0].end()
    depth = 1
    start = pos
    while pos < len(text) and depth:
        if text[pos] == '(': depth += 1
        if text[pos] == ')': depth -= 1
        pos += 1
    if depth:
        raise ValueError('unbalanced idf_component_register(...)')
    tokens = re.findall(r'"[^"]*"|\S+', text[start:pos-1])
    # CMake keywords terminating dependency lists; unsupported keywords fail closed
    keywords = {'SRCS', 'SRC_DIRS', 'EXCLUDE_SRCS', 'INCLUDE_DIRS', 'PRIV_INCLUDE_DIRS', 'REQUIRES', 'PRIV_REQUIRES', 'EMBED_FILES', 'EMBED_TXTFILES', 'WHOLE_ARCHIVE', 'LDFRAGMENTS'}
    deps = []
    mode = None
    for token in tokens:
        u = token.upper()
        if u in keywords:
            mode = u
        elif mode in {'REQUIRES', 'PRIV_REQUIRES'}:
            deps.append(token.strip('"'))
    return deps


def audit(root):
    components = root / 'components'
    if components.is_symlink():
        return ['components is a symlink (unsupported)'], False
    dirs = [components / name for name in LAYERS]
    unknown = [p for p in components.glob('nn2_*') if p.name not in LAYERS] if components.is_dir() else []
    errors = ['unknown layer: ' + str(p.relative_to(root)) for p in unknown]
    present = any(p.exists() or p.is_symlink() for p in dirs)
    if not present and not unknown:
        return [], False
    for name in LAYERS:
        d = components / name
        if d.is_symlink() or not d.is_dir():
            errors.append('missing or symlink layer: ' + name)
            continue
        sources = []
        for p in d.rglob('*'):
            if p.is_symlink():
                errors.append('symlink in layer: ' + str(p.relative_to(root)))
                continue
            if p.is_file() and p.suffix in SOURCE_EXT:
                sources.append(p)
        if not sources:
            errors.append('empty source/header layer: ' + name)
        for p in sources:
            try:
                text = p.read_text(encoding='utf-8')
            except (OSError, UnicodeError) as exc:
                errors.append('unreadable source: ' + str(p) + ': ' + str(exc))
                continue
            for inc in INCLUDE.findall(text):
                # Only match basename for cross-layer headers; platform checks full path and basename.
                base = inc.rsplit('/', 1)[-1]
                if PLATFORM.match(inc) or PLATFORM.match(base) or any(base.startswith(prefix) for prefix in CROSS[name]):
                    errors.append(f'{p.relative_to(root)}: forbidden include {inc}')
        cmake = d / 'CMakeLists.txt'
        if cmake.is_symlink() or not cmake.is_file():
            errors.append('missing or symlink CMakeLists.txt: ' + name)
        else:
            try:
                for dep in cmake_dependencies(cmake.read_text(encoding='utf-8')):
                    if dep not in LAYERS[name]:
                        errors.append(f'{name}: forbidden CMake dependency {dep}')
            except (OSError, UnicodeError, ValueError) as exc:
                errors.append(f'{name}: CMake scan error: {exc}')
    return errors, True


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
    try:
        errors, present = audit(root)
    except Exception as exc:
        print('FAIL: scanner error:', exc)
        return 1
    if errors:
        print('FAIL: Class S dependency gate')
        for e in errors: print(' -', e)
        return 1
    if not present:
        print('NOT APPLICABLE: no normative layer directories present')
        return 3
    print('PASS: ADR-0001 inner-layer include and CMake dependency audit')
    return 0


if __name__ == '__main__':
    sys.exit(main())
