#!/usr/bin/env python3
"""ADR-0001 rev1 Class S gate. Exit 0 PASS, 1 FAIL, 3 NOT APPLICABLE."""
import pathlib
import os
import re
import sys

LAYERS = {"nn2_core": set(), "nn2_ports": {"nn2_core"}, "nn2_app": {"nn2_core", "nn2_ports"}}
SOURCE_EXT = {".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx", ".c++", ".h++", ".s"}
INCLUDE = re.compile(r'^\s*(?:#|%:)\s*include\s*(?:<([^>\n]+)>|"([^"\n]+)")', re.M)
PLATFORM = re.compile(r'^(?:esp_.*|freertos/.*|driver/.*|lwip/.*|hal/.*|soc/.*|rom/.*|esp32s3/rom/.*|xtensa/.*|esp_private/.*|sdkconfig\.h|nimble/.*|host/ble_.*|bt_.*|nvs(?:_.*)?\.h|FreeRTOS\.h|task\.h|queue\.h|semphr\.h|timers\.h|event_groups\.h|nn2_adapters?_.*)$')
CROSS = {"nn2_core": ("nn2_app_", "nn2_port_"), "nn2_ports": ("nn2_app_",), "nn2_app": ()}


def literal_includes(text):
    """Handle C line splicing and comments; macro/transitive/generated includes remain deferred."""
    text = re.sub(r'\\\r?\n', '', text)
    clean = []
    line = ''
    i = 0
    while i < len(text):
        c = text[i]
        if text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0: raise ValueError('unterminated source comment')
            value = ' ' + '\n' * text[i:end+2].count('\n')
            i = end + 2
        elif text.startswith('//', i):
            end = text.find('\n', i + 2)
            value = ' '
            i = len(text) if end < 0 else end
        elif c in ('"', "'"):
            end = i + 1
            while end < len(text) and text[end] != c:
                if text[end] == '\\': end += 1
                end += 1
            if end >= len(text): raise ValueError('unterminated source string/character')
            value = text[i:end+1]
            i = end + 1
        elif c == '<' and re.fullmatch(r'\s*(?:#|%:)\s*include\s*', line):
            # Header names are tokens: // within <driver//gpio.h> is not a comment.
            end = text.find('>', i + 1)
            if end < 0 or '\n' in text[i:end]: raise ValueError('unterminated include header')
            value = text[i:end+1]
            i = end + 1
        else:
            value = c
            i += 1
        clean.append(value)
        line = (line + value).rsplit('\n', 1)[-1]
    return [a or b for a, b in INCLUDE.findall(''.join(clean))]


def layer_files(directory):
    """Enumerate explicitly: pathlib glob may suppress directory permission errors."""
    pending = [directory]
    while pending:
        with os.scandir(pending.pop()) as entries:
            for entry in entries:
                path = pathlib.Path(entry.path)
                if entry.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
                    raise ValueError('symlink/junction in layer: ' + str(path))
                if entry.is_dir(follow_symlinks=False):
                    pending.append(path)
                elif entry.is_file(follow_symlinks=False):
                    yield path
                else:
                    raise ValueError('unsupported file type: ' + str(path))


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
            bracket = re.match(r'#\[(=*)\[', text[i:])
            if bracket:
                end = text.find(']' + bracket[1] + ']', i + len(bracket[0]))
                if end < 0: raise ValueError('unterminated bracket comment')
                i = end + len(bracket[1]) + 2
            else:
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
        elif c == '[':
            raise ValueError('CMake bracket syntax unsupported')
        else:
            j = i
            while j < len(text) and not text[j].isspace() and text[j] not in '()#"':
                j += 1
            if j == i: raise ValueError('unsupported CMake syntax')
            tokens.append(('WORD', text[i:j]))
            i = j
    if any(any(c in value for c in '$;\\[]\x00') for _, value in tokens):
        raise ValueError('dynamic CMake expression, escape, list or bracket argument unsupported')
    # Allow-list the whole script, rather than trying to enumerate every command
    # that can mutate dependencies or indirectly invoke another registration.
    if (len(tokens) < 3 or tokens[0][0] != 'WORD'
            or tokens[0][1].lower() != 'idf_component_register'
            or tokens[1][0] != '(' or tokens[-1][0] != ')'
            or any(kind in ('(', ')') for kind, _ in tokens[2:-1])):
        raise ValueError('expected only one literal idf_component_register command')
    args = tokens[2:-1]
    keywords = {'SRCS', 'SRC_DIRS', 'EXCLUDE_SRCS', 'INCLUDE_DIRS', 'PRIV_INCLUDE_DIRS', 'REQUIRES', 'PRIV_REQUIRES', 'EMBED_FILES', 'EMBED_TXTFILES', 'WHOLE_ARCHIVE', 'LDFRAGMENTS'}
    deps = []
    mode = None
    seen = set()
    for kind, value in args:
        # CMake keyword recognition depends on the value, including quoted values.
        if value in keywords:
            if value in seen: raise ValueError('duplicate registration keyword: ' + value)
            seen.add(value)
            mode = value
        elif mode is None or mode == 'WHOLE_ARCHIVE':
            raise ValueError('unsupported registration argument: ' + value)
        elif mode in {'REQUIRES', 'PRIV_REQUIRES'}:
            deps.append(value)
        else:
            # Literal file/directory inputs must stay inside the audited layer.
            path = pathlib.PurePosixPath(value)
            if path.is_absolute() or '..' in path.parts or ':' in value:
                raise ValueError('registration path outside layer unsupported: ' + value)
    return deps


def audit(root):
    components = root / 'components'
    if not root.is_dir():
        raise ValueError('scan root is not a directory')
    if components.is_symlink() or (hasattr(components, 'is_junction') and components.is_junction()):
        return ['components is a symlink (unsupported)'], False
    if components.exists() and not components.is_dir():
        raise ValueError('components is not a directory')
    dirs = [components / name for name in LAYERS]
    unknown = [p for p in components.iterdir() if p.name.startswith('nn2_') and p.name not in LAYERS] if components.is_dir() else []
    errors = ['unknown layer: ' + str(p.relative_to(root)) for p in unknown]
    present = any(p.exists() or p.is_symlink() for p in dirs)
    if not present and not unknown:
        return [], False
    for name in LAYERS:
        d = components / name
        if d.is_symlink() or (hasattr(d, 'is_junction') and d.is_junction()) or not d.is_dir():
            errors.append('missing or symlink layer: ' + name)
            continue
        sources = []
        for p in layer_files(d):
            if p.name in {'idf_component.yml', 'idf_component.yaml'}:
                errors.append('component-manager dependency manifest unsupported: ' + str(p.relative_to(root)))
            if p.name == 'CMakeLists.txt' and p.parent != d:
                errors.append('nested CMakeLists.txt unsupported: ' + str(p.relative_to(root)))
            if p.suffix.lower() in SOURCE_EXT:
                sources.append(p)
        if not sources:
            errors.append('empty source/header layer: ' + name)
        for p in sources:
            try:
                includes = literal_includes(p.read_text(encoding='utf-8'))
            except (OSError, UnicodeError, ValueError) as exc:
                errors.append('unreadable source: ' + str(p) + ': ' + str(exc))
                continue
            for inc in includes:
                parts = pathlib.PurePosixPath(inc.replace('\\', '/')).parts
                suffixes = ['/'.join(parts[i:]) for i in range(len(parts))]
                forbidden_layers = set(LAYERS) - LAYERS[name] - {name}
                if (any(PLATFORM.fullmatch(s) for s in suffixes)
                        or any(part.startswith(CROSS[name]) for part in parts)
                        or forbidden_layers.intersection(parts)):
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
    try:
        root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
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
