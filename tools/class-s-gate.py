#!/usr/bin/env python3
"""ADR-0001 rev1 Class S gate. Exit 0 PASS, 1 FAIL, 3 NOT APPLICABLE."""
import pathlib
import os
import re
import sys
import argparse

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from layer_contract import MANIFEST, detect, load_contract, match_adapter_component

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


class _Cmd(object):
    def __init__(self, name, args):
        self.name = name
        self.args = args


_VAR = re.compile(r'^\$\{([A-Za-z_][A-Za-z0-9_]*)\}$')
_IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')
_NON_DEP_COMMANDS = {
    'target_compile_definitions', 'target_compile_options', 'target_include_directories',
    'message', 'execute_process',
}
_REGISTER_KEYWORDS = {
    'SRCS', 'SRC_DIRS', 'EXCLUDE_SRCS', 'INCLUDE_DIRS', 'PRIV_INCLUDE_DIRS', 'REQUIRES',
    'PRIV_REQUIRES', 'EMBED_FILES', 'EMBED_TXTFILES', 'WHOLE_ARCHIVE', 'LDFRAGMENTS',
}


def _reject_hidden_token(value):
    if any(c in value for c in ';\\[]\x00'):
        raise ValueError('dynamic CMake expression, escape, list or bracket argument unsupported')


def _cmake_tokens(text):
    """Lex a restricted CMake subset. ${...} stays intact for later resolution."""
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
            value = text[i+1:j]
            _reject_hidden_token(value)
            tokens.append(('STRING', value))
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
            value = text[i:j]
            _reject_hidden_token(value)
            tokens.append(('WORD', value))
            i = j
    return tokens


def _commands(tokens):
    cmds = []
    i = 0
    while i < len(tokens):
        kind, name = tokens[i]
        if kind != 'WORD':
            raise ValueError('unparseable CMake dependency construct: ' + name)
        if i + 1 >= len(tokens) or tokens[i + 1][0] != '(':
            raise ValueError('unparseable CMake dependency construct: ' + name)
        i += 2
        args = []
        depth = 1
        while i < len(tokens) and depth:
            kind, value = tokens[i]
            if kind == '(':
                depth += 1
                if depth > 1:
                    args.append((kind, value))
            elif kind == ')':
                depth -= 1
                if depth:
                    args.append((kind, value))
            else:
                args.append((kind, value))
            i += 1
        if depth:
            raise ValueError('unparseable CMake dependency construct: unterminated ' + name)
        cmds.append(_Cmd(name, args))
    return cmds


def _copy_env(env):
    return {key: set(values) for key, values in env.items()}


def _merge_envs(envs):
    merged = {}
    keys = set()
    for env in envs:
        keys.update(env)
    for key in keys:
        values = set()
        for env in envs:
            if key in env:
                values.update(env[key])
        merged[key] = values
    return merged


def _expand_stored(token, env):
    """Expand a known literal variable. Leave unresolved tokens marked with $."""
    match = _VAR.fullmatch(token)
    if match and match.group(1) in env:
        parts = []
        tainted = False
        for alt in sorted(env[match.group(1)]):
            for part in alt:
                parts.append(part)
                if '$' in part:
                    tainted = True
        if tainted:
            return [token]
        return parts
    return [token]


def _require_resolved(token, env):
    match = _VAR.fullmatch(token)
    if match:
        if match.group(1) not in env:
            raise ValueError('unresolved CMake variable in dependency position: ' + token)
        resolved = []
        for alt in sorted(env[match.group(1)]):
            for part in alt:
                if '$' in part:
                    raise ValueError('unresolved CMake variable in dependency position: ' + part)
                resolved.append(part)
        return resolved
    if '$' in token:
        raise ValueError('unresolved CMake variable in dependency position: ' + token)
    return [token]


def _check_layer_path(value):
    path = pathlib.PurePosixPath(value)
    if path.is_absolute() or '..' in path.parts or ':' in value:
        raise ValueError('registration path outside layer unsupported: ' + value)


def _plain_values(cmd):
    values = []
    for kind, value in cmd.args:
        if kind in ('(', ')'):
            raise ValueError('unparseable CMake dependency construct: nested parentheses in ' + cmd.name)
        values.append(value)
    return values


def _apply_set(env, cmd):
    args = _plain_values(cmd)
    if (not args or not _IDENT.fullmatch(args[0])
            or any(part in ('CACHE', 'PARENT_SCOPE') for part in args)):
        raise ValueError('unparseable CMake dependency construct: set')
    stored = []
    for token in args[1:]:
        stored.extend(_expand_stored(token, env))
    env = _copy_env(env)
    env[args[0]] = {tuple(stored)}
    return env


def _apply_list(env, cmd):
    args = _plain_values(cmd)
    if len(args) < 2 or args[0] != 'APPEND' or not _IDENT.fullmatch(args[1]):
        raise ValueError('unparseable CMake dependency construct: list')
    name = args[1]
    if name not in env:
        raise ValueError('unparseable CMake dependency construct: list(APPEND) of unset ' + name)
    extra = []
    for token in args[2:]:
        extra.extend(_expand_stored(token, env))
    env = _copy_env(env)
    env[name] = {alt + tuple(extra) for alt in env[name]}
    return env


def _registration_dependencies(cmd, env):
    if any(kind in ('(', ')') for kind, _ in cmd.args):
        raise ValueError('expected only one literal idf_component_register command')
    deps = []
    mode = None
    seen = set()
    for _, value in cmd.args:
        if value in _REGISTER_KEYWORDS:
            if value in seen:
                raise ValueError('duplicate registration keyword: ' + value)
            seen.add(value)
            mode = value
            continue
        if mode is None or mode == 'WHOLE_ARCHIVE':
            raise ValueError('unsupported registration argument: ' + value)
        if mode in {'REQUIRES', 'PRIV_REQUIRES'}:
            deps.extend(_require_resolved(value, env))
        else:
            for part in _require_resolved(value, env):
                _check_layer_path(part)
    return deps


def _split_if(cmds, start):
    branches = [[]]
    depth = 0
    saw_else = False
    i = start + 1
    while i < len(cmds):
        name = cmds[i].name.lower()
        if name == 'if':
            depth += 1
            branches[-1].append(cmds[i])
        elif name == 'endif' and depth == 0:
            return branches, i, saw_else
        elif name == 'endif':
            depth -= 1
            branches[-1].append(cmds[i])
        elif name in ('elseif', 'else') and depth == 0:
            if saw_else:
                raise ValueError('unparseable CMake dependency construct: ' + name)
            if name == 'else':
                saw_else = True
            branches.append([])
        else:
            branches[-1].append(cmds[i])
        i += 1
    raise ValueError('unparseable CMake dependency construct: unterminated if')


def _eval_commands(cmds, env):
    """Union every if/else branch. Each path must register exactly once."""
    env = _copy_env(env)
    deps = []
    min_regs = 0
    max_regs = 0
    i = 0
    while i < len(cmds):
        name = cmds[i].name.lower()
        if name == 'if':
            branches, i, saw_else = _split_if(cmds, i)
            if not saw_else:
                branches.append([])
            branch_envs = []
            mins = []
            maxs = []
            for branch in branches:
                found, lo, hi, branch_env = _eval_commands(branch, env)
                deps.extend(found)
                mins.append(lo)
                maxs.append(hi)
                branch_envs.append(branch_env)
            min_regs += min(mins)
            max_regs += max(maxs)
            env = _merge_envs(branch_envs)
        elif name in ('elseif', 'else', 'endif'):
            raise ValueError('unparseable CMake dependency construct: ' + name)
        elif name == 'idf_component_register':
            min_regs += 1
            max_regs += 1
            deps.extend(_registration_dependencies(cmds[i], env))
        elif name in _NON_DEP_COMMANDS:
            pass
        elif name == 'set':
            env = _apply_set(env, cmds[i])
        elif name == 'list':
            env = _apply_list(env, cmds[i])
        else:
            raise ValueError('unparseable CMake dependency construct: ' + cmds[i].name)
        i += 1
    return deps, min_regs, max_regs, env


def cmake_dependencies(text):
    """Collect REQUIRES and PRIV_REQUIRES, or fail closed.

    if/elseif/else contribute the union of every branch, including the implicit
    false branch of an if without else. A ${...} the parser cannot reduce to
    literals is an error in a dependency or registered-path position, never a skip.
    target_compile_definitions and target_compile_options are not dependencies.
    """
    deps, lo, hi, _env = _eval_commands(_commands(_cmake_tokens(text)), {})
    if lo != 1 or hi != 1:
        raise ValueError('expected only one literal idf_component_register command')
    unique = []
    for dep in deps:
        if dep not in unique:
            unique.append(dep)
    return unique


def _dependency_allowed(spec, dep):
    if spec.get('allow_any_dependency'):
        return True
    if (dep in spec.get('forbidden_dependencies', ())
            or any(re.fullmatch(pattern, dep) for pattern in spec.get('forbidden_dependency_patterns', ()))):
        return False
    if dep in spec.get('allowed_dependencies', ()):
        return True
    if dep.startswith('nn2_'):
        return False
    return bool(spec.get('allow_idf_dependencies'))


def _token(part):
    return part.split('.', 1)[0]


def _is_own_header(part, component):
    token = _token(part)
    return token == component or part.startswith(component + '_') or part.startswith(component + '.')


def _include_forbidden(spec, inc, component):
    parts = pathlib.PurePosixPath(inc.replace('\\', '/')).parts
    if not parts:
        return True
    suffixes = ['/'.join(parts[i:]) for i in range(len(parts))]
    base = parts[-1]
    if spec.get('allow_sdkconfig') and (base == 'sdkconfig.h' or 'sdkconfig.h' in suffixes):
        return False
    if spec.get('allow_any_include'):
        return False
    for part in parts:
        token = _token(part)
        if component and (_is_own_header(part, component) or token == component):
            continue
        if spec.get('forbid_other_adapters') and (
                match_adapter_component(token) or match_adapter_component(part)):
            return True
        if any(part.startswith(prefix) or token.startswith(prefix)
               for prefix in spec.get('forbidden_include_prefixes', ())):
            return True
        if token in spec.get('forbidden_components', ()) or part in spec.get('forbidden_components', ()):
            return True
    if spec.get('use_global_regex') and any(
            re.fullmatch(pattern, suffix) for pattern in spec.get('forbidden_include_regex', ())
            for suffix in suffixes):
        return True
    cross = spec.get('cross_prefixes', ())
    if any(part.startswith(cross) for part in parts):
        return True
    if spec.get('forbidden_layers', set()).intersection(parts):
        return True
    return False


def _scan_tree(root, directory, name, spec, extensions, errors):
    if directory.is_symlink() or (hasattr(directory, 'is_junction') and directory.is_junction()) or not directory.is_dir():
        errors.append('missing or symlink layer: ' + name)
        return
    sources = []
    for path in layer_files(directory):
        if path.name in {'idf_component.yml', 'idf_component.yaml'}:
            errors.append('component-manager dependency manifest unsupported: ' + str(path.relative_to(root)))
        if path.name == 'CMakeLists.txt' and path.parent != directory:
            errors.append('nested CMakeLists.txt unsupported: ' + str(path.relative_to(root)))
        if path.suffix.lower() in extensions:
            sources.append(path)
    if not sources:
        errors.append('empty source/header layer: ' + name)
    component = spec.get('component')
    for path in sorted(sources):
        try:
            includes = literal_includes(path.read_text(encoding='utf-8'))
        except (OSError, UnicodeError, ValueError) as exc:
            errors.append('unreadable source: ' + str(path) + ': ' + str(exc))
            continue
        for inc in includes:
            if _include_forbidden(spec, inc, component):
                errors.append(f'{path.relative_to(root)}: forbidden include {inc}')
    cmake = directory / 'CMakeLists.txt'
    if cmake.is_symlink() or not cmake.is_file():
        errors.append('missing or symlink CMakeLists.txt: ' + name)
        return
    try:
        for dep in cmake_dependencies(cmake.read_text(encoding='utf-8')):
            if not _dependency_allowed(spec, dep):
                errors.append(f'{name}: forbidden CMake dependency {dep}')
    except (OSError, UnicodeError, ValueError) as exc:
        errors.append(f'{name}: CMake scan error: {exc}')


def _inner_spec(row, rows, outer, patterns):
    layers = {pathlib.Path(item['path']).name: set(item['requires']) for item in rows}
    name = pathlib.Path(row['path']).name
    forbidden_layers = set(layers) - layers[name] - {name}
    for item in outer:
        if item.get('component') and item['component'] != 'main':
            forbidden_layers.add(item['component'])
    cross = [prefix for item in rows if pathlib.Path(item['path']).name in forbidden_layers
             for prefix in item['header_prefixes']]
    for item in outer:
        cross.extend(item['header_prefixes'])
    return {
        'component': name,
        'allowed_dependencies': list(row['requires']),
        'use_global_regex': True,
        'forbidden_include_regex': patterns,
        'cross_prefixes': tuple(cross),
        'forbidden_layers': forbidden_layers,
        'forbid_other_adapters': True,
    }


def _outer_spec(row, component):
    spec = {
        'component': component,
        'allowed_dependencies': list(row.get('allowed_dependencies', ())),
        'forbidden_dependencies': list(row.get('forbidden_dependencies', ())),
        'forbidden_dependency_patterns': list(row.get('forbidden_dependency_patterns', ())),
        'allow_idf_dependencies': bool(row.get('allow_idf_dependencies')),
        'allow_any_dependency': bool(row.get('allow_any_dependency')),
        'forbidden_include_prefixes': list(row.get('forbidden_include_prefixes', ())),
        'allow_sdkconfig': bool(row.get('allow_sdkconfig')),
        'allow_any_include': bool(row.get('allow_any_dependency') and row.get('allow_platform_includes')),
        'forbidden_components': list(row.get('forbidden_dependencies', ())),
        'forbid_other_adapters': row['name'] == 'adapter',
    }
    return spec


def _outer_directories(root, row):
    if row['name'] == 'adapter':
        base = root / 'components'
        if not base.is_dir():
            return []
        found = []
        with os.scandir(base) as entries:
            for entry in sorted(entries, key=lambda item: item.name):
                if match_adapter_component(entry.name):
                    found.append((entry.name, pathlib.Path(entry.path)))
        return found
    directory = root / row['path']
    if directory.exists():
        return [(row['component'], directory)]
    return []


def audit(root, manifest=MANIFEST):
    policy = load_contract(manifest)  # Validate before any NOT APPLICABLE verdict.
    rows = policy['layers']
    outer = policy.get('outer_layers') or []
    try:
        present = detect(root, rows, outer)
    except ValueError as exc:
        return [str(exc)], True
    if not present:
        return [], False
    errors = []
    extensions = policy['source_extensions']
    for row in rows:
        name = pathlib.Path(row['path']).name
        _scan_tree(root, root / row['path'], name, _inner_spec(row, rows, outer, policy['forbidden_include_regex']),
                   extensions, errors)
    for row in outer:
        for component, directory in _outer_directories(root, row):
            _scan_tree(root, directory, component, _outer_spec(row, component), extensions, errors)
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
    print('PASS: ADR-0001 inner-layer include and CMake dependency audit; schema v2 outer rows included')
    return 0


if __name__ == '__main__':
    sys.exit(main())
