#!/usr/bin/env python3
"""Audit the module surface against the public headers.

src/recastnavigation.cppm must be exactly what tools/surface.py writes from
tools/surface.json, and every name of that record must be found in a header
under upstream/recastnavigation/*/Include. With a clang++ (--clang, or
MWRAP_CLANG) the record itself is derived again from the headers as the
compiler reads them and must be the same: no public name is left out, none is
exported that is not there. MWRAP_REQUIRE_CLANG=1 makes the second half
mandatory (CI's LLVM row).
"""
import argparse
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import surface  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default=os.environ.get('MWRAP_CLANG'))
    args = parser.parse_args()
    record = surface.record()
    written = surface.MODULE.read_text(encoding='utf-8')
    assert written == surface.module_text(record), 'src/recastnavigation.cppm is not what tools/surface.py --write gives'
    headers = ''.join(path.read_text(encoding='utf-8') for directory in surface.include_dirs() for path in sorted(directory.glob('*.h')))
    count = exported = 0
    for kind, names in record.items():
        for name in names:
            count += 1
            # An operator is spelled with or without a space before its symbol.
            pattern = r'\boperator\s*' + re.escape(name.removeprefix('operator').strip()) if name.startswith('operator') \
                else r'(?<![\w])' + re.escape(name) + r'(?![\w])'
            assert re.search(pattern, headers), f'{name} is in no header'
            if kind == 'internal':
                continue
            if kind == 'adapted':
                assert re.search(rf'inline constexpr [\w ]+ {re.escape(name)} = ', written), name
            else:
                exported += 1
                assert f'export using ::{name};' in written, name
    if args.clang:
        derived = surface.derive(args.clang)
        assert derived == record, sorted(set(sum((list(v) for v in derived.values()), [])) ^ set(sum((list(v) for v in record.values()), [])))
    else:
        assert os.environ.get('MWRAP_REQUIRE_CLANG') != '1', 'MWRAP_REQUIRE_CLANG=1 and no clang++ given'
    internal = len(record.get('internal', []))
    print(f'{count} names: {exported} exported, {count - exported - internal} constants of internal linkage adapted, {internal} functions of internal '
          'linkage not exported; ' + ('the record is what clang derives from the headers' if args.clang else 'the record not derived again (no clang++ given)'))


if __name__ == '__main__':
    main()
