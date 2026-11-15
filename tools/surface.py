#!/usr/bin/env python3
"""The module's export list, derived from Recast & Detour's headers.

With --clang <clang++> the headers are read by the compiler as this package
builds the library (mcpp.toml): every name declared at global scope by a
header under upstream/recastnavigation/*/Include is recorded in
tools/surface.json, by kind. src/recastnavigation.cppm is written from that
record (--write), one `using` a name: a using-declaration carries every
overload and every template of the name.

Recast & Detour declare their names in the global namespace (`rc…`, `dt…`),
so the module exports them there, unchanged.

An object at namespace scope declared `static` (the headers' `static const`
constants: DT_VERTS_PER_POLYGON, RC_WALKABLE_AREA, DT_SUCCESS …) has
internal linkage: each translation unit that includes the header has its own,
and a module cannot export it. Those are recorded as `adapted` and declared
again, with their names, types and values, in `mcpplibs::mwrap::recastnavigation`
(an integer constant from the header's, a float one from its literal, read by
clang). A function of internal linkage would be recorded `internal` and not
exported (there are none).

The enumerators of an unscoped enumeration are names of the global namespace:
they are recorded (`enumerators`) and exported with it.

Not read: DebugUtils (not vendored); the assert hooks, which exist only in a build without NDEBUG. The headers' macros (rcAssert, dtAssert,
rcLikely, RC_SIZE_MAX) are spelled by the library's own sources only.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
UPSTREAM = ROOT / 'upstream/recastnavigation'
PARTS = ('Recast', 'Detour', 'DetourCrowd', 'DetourTileCache')
RECORD = ROOT / 'tools/surface.json'
MODULE = ROOT / 'src/recastnavigation.cppm'
TARGET = 'mcpplibs::mwrap::recastnavigation'
# As the package builds the library (mcpp.toml's cxxflags): NDEBUG in every profile, so the assert hooks
# (rcAssertFailSetCustom …, declared only without it) are not part of the surface in one profile and absent
# from it in another.
DEFINES = ('NDEBUG',)
KINDS = {
    'CXXRecordDecl': 'types', 'ClassTemplateDecl': 'types', 'EnumDecl': 'types', 'TypeAliasDecl': 'types', 'TypedefDecl': 'types',
    'TypeAliasTemplateDecl': 'types', 'FunctionDecl': 'functions', 'FunctionTemplateDecl': 'functions', 'VarDecl': 'constants',
}
# Declarations that name nothing of their own.
PASSED = {'UsingDirectiveDecl', 'UsingDecl', 'ClassTemplateSpecializationDecl', 'ClassTemplatePartialSpecializationDecl', 'StaticAssertDecl',
          'EmptyDecl', 'CXXMethodDecl', 'CXXConstructorDecl', 'CXXDestructorDecl', 'CXXConversionDecl', 'LinkageSpecDecl', 'FullComment'}


def include_dirs():
    return [UPSTREAM / part / 'Include' for part in PARTS]


def headers():
    """Every public header, in a stable order (each includes what it needs)."""
    return sorted(path.name for directory in include_dirs() for path in directory.glob('*.h'))


def is_upstream(file):
    return file is not None and Path(file).resolve().is_relative_to(UPSTREAM.resolve())


def top_level(clang):
    """The translation unit's top-level declarations, each with the file it is declared in."""
    with tempfile.TemporaryDirectory(prefix='recast-surface-') as directory:
        source = Path(directory) / 'all.cpp'
        source.write_text(''.join(f'#include <{header}>\n' for header in headers()), encoding='utf-8')
        command = [clang, '-std=c++23', '-fsyntax-only', *[f'-D{define}' for define in DEFINES], *[f'-I{path}' for path in include_dirs()], f'-I{ROOT / "port"}', '-Xclang', '-skip-function-bodies',
                   '-Xclang', '-ast-dump=json', str(source)]
        tree = json.loads(subprocess.run(command, check=True, stdout=subprocess.PIPE, text=True).stdout)
    # clang writes a location's file only where it changes from the location printed before: follow it in order.
    current = [None]

    def follow(node):
        if isinstance(node, dict):
            for key in ('loc', 'range'):
                if key in node:
                    track(node[key])
            for child in node.get('inner', []):
                follow(child)

    def track(location):
        if isinstance(location, dict):
            if 'file' in location:
                current[0] = location['file']
            for key in ('spellingLoc', 'expansionLoc', 'begin', 'end'):
                if key in location:
                    track(location[key])

    out = []
    for node in tree.get('inner', []):
        if 'loc' in node:
            track(node['loc'])
        declared = current[0]
        follow({'inner': node.get('inner', []), 'range': node.get('range', {})})
        out.append((node, declared))
    return out


def literal(node):
    """A float constant's value as clang read its initialiser."""
    for child in node.get('inner', []):
        if child.get('kind') == 'FloatingLiteral':
            return child['value']
        found = literal(child)
        if found is not None:
            return found
    return None


def derive(clang):
    kinds = {'types': set(), 'enumerators': set(), 'functions': set(), 'constants': set(), 'adapted': {}, 'internal': set()}
    unknown = set()
    for node, file in top_level(clang):
        kind, name = node.get('kind'), node.get('name')
        if not is_upstream(file) or node.get('isImplicit') or not name:
            continue
        if kind == 'VarDecl' and node.get('storageClass') == 'static':
            qualified = node.get('type', {}).get('qualType', '')
            if 'float' in qualified:
                value = literal(node)
                assert value is not None, name
                kinds['adapted'][name] = {'type': 'float', 'value': value}
            else:
                kinds['adapted'][name] = {'type': qualified.removeprefix('const ')}
        elif kind == 'FunctionDecl' and node.get('storageClass') == 'static':
            kinds['internal'].add(name)
        elif kind in KINDS:
            kinds[KINDS[kind]].add(name)
            if kind == 'EnumDecl' and not node.get('scopedEnumTag'):
                kinds['enumerators'] |= {item['name'] for item in node.get('inner', []) if item.get('kind') == 'EnumConstantDecl'}
        elif kind not in PASSED:
            unknown.add(f'{kind} {name}')
    assert not unknown, sorted(unknown)
    return {kind: (dict(sorted(names.items())) if isinstance(names, dict) else sorted(names)) for kind, names in kinds.items() if names}


def record():
    return json.loads(RECORD.read_text(encoding='utf-8'))


def module_text(surface=None):
    surface = surface or record()
    lines = ['// mcpplibs.mwrap.recastnavigation — Recast & Detour, the navigation-mesh construction and path-finding',
             '// libraries, as a C++ module: Recast (navmesh building), Detour (the navmesh and its queries), DetourCrowd',
             '// (crowds of agents moving on it) and DetourTileCache (obstacles in tiled meshes). Every exported name is',
             "// the upstream entity of the headers, in the global namespace where upstream declares it.",
             '// Written by tools/surface.py from the headers as the compiler reads them (tools/surface.json;',
             '// tools/check_exports.py holds the three together).',
             'module;',
             *[f'#include <{header}>' for header in headers()],
             '',
             'export module mcpplibs.mwrap.recastnavigation;',
             '']
    for kind, title in (('types', 'types'), ('enumerators', 'enumerators of unscoped enumerations'), ('constants', 'objects'),
                        ('functions', 'functions and operators')):
        names = surface.get(kind, [])
        if names:
            lines.append(f'// {title}')
            lines += [f'export using ::{name};' for name in names]
            lines.append('')
    adapted = surface.get('adapted', {})
    if adapted:
        lines += ["// The headers' constants of internal linkage (`static const`: every includer has its own, and none can be",
                  '// exported), again under their names with their types and values.',
                  f'export namespace {TARGET} {{']
        for name, spec in adapted.items():
            if spec['type'] == 'float':
                value = spec['value']
                lines.append(f'inline constexpr float {name} = {value}{"" if any(c in value for c in ".e") else ".0"}f;')
            else:
                lines.append(f'inline constexpr {spec["type"]} {name} = ::{name};')
        lines += [f'}}  // namespace {TARGET}', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', help='derive tools/surface.json with this clang++')
    parser.add_argument('--write', action='store_true', help='write src/recastnavigation.cppm')
    args = parser.parse_args()
    if args.clang:
        RECORD.write_text(json.dumps(derive(args.clang), indent=1) + '\n', encoding='utf-8')
    if args.write:
        MODULE.write_text(module_text(), encoding='utf-8')
    print(', '.join(f'{len(names)} {kind}' for kind, names in record().items()))


if __name__ == '__main__':
    sys.exit(main())
