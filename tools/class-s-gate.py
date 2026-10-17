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
    """Parse one literal idf_component_register invocation, reject dynamic/ambiguous CMake.

    This is intentionally a restricted CMake subset, not a general CMake interpreter.
    """
    # Lexer: comments, quoted strings, parentheses, and plain words.
    tokens = []
    i = 0
    while i < len(text):
        c = text[i]
        if c.isspace():
            i += 1
        elif c == '#':
            i = text.find('\n', i)
            if i < 0: break
        elif c == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                if text[j] == '\\': j += 1
                j += 1
            if j >= len(text): raise ValueError('unterminated quoted string')
            tokens.append(('STRING', text[i+1:j]))
            i = j + 1
        elif c in '()':
            tokens.append((c, c))
            i += 1
        elif c == '[' and re.match(r'\[=+\[', text[i:]):
            raise ValueError('CMake bracket syntax unsupported')
        else:
            j = i
            while j < len(text) and not text[j].isspace() and text[j] not in '()#"':
                j += 1
            if j == i: raise ValueError('unsupported CMake syntax')
            tokens.append(('WORD', text[i:j]))
            i = j
    if any('${' in value or '$<' in value or '$ENV{' in value or ';' in value for _, value in tokens):
        raise ValueError('dynamic CMake expression or list unsupported')
    calls = [i for i, tok in enumerate(tokens) if tok == ('WORD', 'idf_component_register')]
    if len(calls) != 1: raise ValueError('expected exactly one idf_component_register')
    start = calls[0]
    if start+1 >= len(tokens) or tokens[start+1][0] != '(':
        raise ValueError('invalid idf_component_register syntax')
    depth = 1
    args = []
    j = start + 2
    while j < len(tokens) and depth:
        kind, value = tokens[j]
        if kind == '(': depth += 1
        elif kind == ')': depth -= 1
        if depth and kind in ('WORD', 'STRING'): args.append((kind, value))
        if depth and kind in ('(', ')'): raise ValueError('nested CMake expressions unsupported')
        j += 1
    if depth: raise ValueError('unbalanced idf_component_register')
    keywords = {'SRCS', 'SRC_DIRS', 'EXCLUDE_SRCS', 'INCLUDE_DIRS', 'PRIV_INCLUDE_DIRS', 'REQUIRES', 'PRIV_REQUIRES', 'EMBED_FILES', 'EMBED_TXTFILES', 'WHOLE_ARCHIVE', 'LDFRAGMENTS'}
    deps = []
    mode = None
    for kind, value in args:
        if kind == 'WORD' and value.upper() in keywords:
            mode = value.upper()
        elif mode in {'REQUIRES', 'PRIV_REQUIRES'}:
            deps.append(value)
    # Additional CMake commands can modify dependency properties after registration.
    # Refuse potentially dynamic scripts until a complete dependency evaluator exists.
    remainder = tokens[:start] + tokens[j:]
    if any(kind == 'WORD' and value.lower() in {'set', 'list', 'include', 'add_subdirectory', 'if', 'foreach', 'function', 'macro', 'execute_process', 'set_property', 'set_target_properties', 'target_link_libraries'} for kind, value in remainder):
        raise ValueError('unsupported CMake dependency-mutating command')
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
