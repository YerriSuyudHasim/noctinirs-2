#!/usr/bin/env python3
"""ADR-0001 rev1 Class S gate. Exit 0 PASS, 1 FAIL, 3 NOT APPLICABLE."""
import pathlib
import os
import re
import sys
import argparse

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from layer_contract import MANIFEST, detect, load_contract

INCLUDE = re.compile(r'^\s*(?:#|%:)\s*include\s*(?:<([^>\n]+)>|"([^"\n]+)")', re.M)


def literal_includes(text):
    """Scan literal includes; macro/transitive/generated includes remain deferred."""
    text = text.removeprefix('\ufeff')  # One optional UTF-8 signature at source start.
    trigraphs = {'=': '#', '/': '\\', "'": '^', '(': '[', ')': ']',
                '!': '|', '<': '{', '>': '}', '-': '~'}
    # C11 phase 1 precedes phase 2 splicing and phase 3 comment recognition.
    translated = re.sub(r'\?\?([=/\'()!<>-])', lambda m: trigraphs[m[1]], text)
    includes = unspliced_literal_includes(translated)
    if translated != text:
        # Also cover modes where trigraphs are inactive. Conversion must not
        # hide an ordinary directive behind a newly spliced // comment.
        includes += unspliced_literal_includes(text)
    return includes


def unspliced_literal_includes(text):
    """Apply line splicing, then lex comments and literal header tokens."""
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
            for entry in sorted(entries, key=lambda x: x.name):
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


def audit(root, manifest=MANIFEST):
    policy = load_contract(manifest)  # Validate before any NOT APPLICABLE verdict.
    rows = policy['layers']
    if not detect(root, rows):
        return [], False
    layers = {pathlib.Path(r['path']).name: set(r['requires']) for r in rows}
    errors = []
    for row in rows:
        name = pathlib.Path(row['path']).name
        d = root / row['path']
        if d.is_symlink() or (hasattr(d, 'is_junction') and d.is_junction()) or not d.is_dir():
            errors.append('missing or symlink layer: ' + name)
            continue
        sources = []
        for p in layer_files(d):
            if p.name in {'idf_component.yml', 'idf_component.yaml'}:
                errors.append('component-manager dependency manifest unsupported: ' + str(p.relative_to(root)))
            if p.name == 'CMakeLists.txt' and p.parent != d:
                errors.append('nested CMakeLists.txt unsupported: ' + str(p.relative_to(root)))
            if p.suffix.lower() in policy['source_extensions']:
                sources.append(p)
        if not sources:
            errors.append('empty source/header layer: ' + name)
        forbidden_layers = set(layers) - layers[name] - {name}
        cross = tuple(prefix for r in rows if pathlib.Path(r['path']).name in forbidden_layers
                      for prefix in r['header_prefixes'])
        for p in sorted(sources):
            try:
                includes = literal_includes(p.read_text(encoding='utf-8'))
            except (OSError, UnicodeError, ValueError) as exc:
                errors.append('unreadable source: ' + str(p) + ': ' + str(exc))
                continue
            for inc in includes:
                parts = pathlib.PurePosixPath(inc.replace('\\', '/')).parts
                suffixes = ['/'.join(parts[i:]) for i in range(len(parts))]
                if (any(re.fullmatch(pattern, s) for pattern in policy['forbidden_include_regex'] for s in suffixes)
                        or any(part.startswith(cross) for part in parts)
                        or forbidden_layers.intersection(parts)):
                    errors.append(f'{p.relative_to(root)}: forbidden include {inc}')
        cmake = d / 'CMakeLists.txt'
        if cmake.is_symlink() or not cmake.is_file():
            errors.append('missing or symlink CMakeLists.txt: ' + name)
        else:
            try:
                for dep in cmake_dependencies(cmake.read_text(encoding='utf-8')):
                    if dep not in layers[name]:
                        errors.append(f'{name}: forbidden CMake dependency {dep}')
            except (OSError, UnicodeError, ValueError) as exc:
                errors.append(f'{name}: CMake scan error: {exc}')
    return errors, True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', nargs='?', default='.')
    parser.add_argument('--manifest', type=pathlib.Path, default=MANIFEST)
    args = parser.parse_args()
    try:
        root = pathlib.Path(args.root).resolve()
        errors, present = audit(root, args.manifest)
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
