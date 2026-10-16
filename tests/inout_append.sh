#!/bin/sh
# Bound allocation growth, not noisy wall time; a pure-concat control must fail.
set -eu
cd "$(dirname "$0")/.."
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM
python3 - "$t" <<'PY'
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

root = Path.cwd()
tmp = Path(sys.argv[1])
env = dict(os.environ, TYCHO_CORELIB=str(root / 'corelib'))
source = '''fn append(s: inout string, piece: string, n: int):
    for i := 0; i < n; i += 1:
        s = s + piece
fn main():
    s := ""
    piece := "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ__"
    append(&s, piece, COUNT)
    for i := 0; i < len(s); i += 1:
        if s[i] != piece[i % len(piece)]:
            die("append contents differ")
    println(str(len(s)) + "," + str(s[0]) + "," + str(s[len(s) - 1]))
'''


def measure(compiler, count, control=False, mode='loop', binary=False):
    stem = tmp / (compiler + '-' + mode + '-' + str(count) + ('-bytes' if binary else '') +
                  ('-control' if control else ''))
    text = source.replace('COUNT', str(count))
    if mode in ('calls', 'tuple', 'conditional', 'match', 'generic'):
        text = text.replace('    append(&s, piece, ' + str(count) + ')',
                            '    for j := 0; j < ' + str(count) + '; j += 1:\n        append(&s, piece, 1)')
    elif mode == 'forwarded':
        text = text.replace('fn main():',
                            'fn relay(s: inout string, piece: string):\n    append(&s, piece, 1)\nfn main():')
        text = text.replace('    append(&s, piece, ' + str(count) + ')',
                            '    for j := 0; j < ' + str(count) + '; j += 1:\n        relay(&s, piece)')
    if mode == 'generic':
        text = text.replace('s: inout string, piece: string', 's: inout $T, piece: $T')
    elif mode == 'tuple':
        text = text.replace('    s := ""', '    s, unused := ("", 0)')
    elif mode == 'conditional':
        text = text.replace('    s := ""', '    s := if true:\n        ""\n    else:\n        "unused"')
    elif mode == 'match':
        text = 'enum Box:\n    Text(string)\n' + text
        text = text.replace('    s := ""\n', '')
        head, body = text.split('fn main():\n')
        text = head + 'fn main():\n    match Text(""):\n        Text(s):\n' + ''.join(
            '        ' + line + '\n' for line in body.splitlines())
    if binary:
        text = text.replace('inout string', 'inout bytes').replace('piece: string', 'piece: bytes')
        text = text.replace('s := ""', 's := to_bytes("")')
        text = text.replace('"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ__"',
                            'to_bytes([' + ','.join(['0', '65'] * 32) + '])')
    if control:
        # Same bytes and interface, but no self-append left-spine leaf.
        text = text.replace('s = s + piece', 's = "" + s + piece')
    hi = stem.with_suffix('.ty')
    hi.write_text(text)
    subprocess.run([str(root / compiler), str(hi), '--emit-c', '-o', str(stem)],
                   env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                   ['-O2', '-fwrapv', str(stem.with_suffix('.c')), '-lm', '-pthread', '-o', str(stem)],
                   check=True)
    runenv = dict(env, TYCHO_ARENA_STATS='full')
    result = subprocess.run([str(stem)], env=runenv, check=True, capture_output=True,
                            text=True, timeout=30)
    ends = '0,65' if binary else '97,95'
    assert result.stdout == f'{count * 64},{ends}\n', result.stdout
    row = re.search(r'bump-alloc:\s+([\d.]+) (B|KiB|MiB|GiB) over (\d+) allocations', result.stderr)
    assert row, result.stderr
    size = float(row[1]) * {'B': 1, 'KiB': 1024, 'MiB': 1024**2, 'GiB': 1024**3}[row[2]]
    return size


for compiler in ('tychoc', 'tychoc1'):
    for mode in ('loop', 'calls', 'forwarded', 'tuple', 'conditional', 'match', 'generic'):
        for count in (128, 1024):
            for binary in ((False, True) if mode in ('calls', 'forwarded') else (False,)):
                size = measure(compiler, count, mode=mode, binary=binary)
                bound = count * 64 * 8
                label = f'{compiler} {mode}' + (' bytes' if binary else '')
                assert size < bound, f'{label}: {count} chunks bump {size} exceeds {bound}'
                print(f'inout-append: {label} {count} chunks, {size:.0f} B bump < {bound} B')
    size = measure(compiler, 128, control=True)
    assert size > 128 * 64 * 8, f'{compiler}: pure-concat control did not exceed the bound'
    print(f'inout-append: {compiler} pure-concat control rejected ({size:.0f} B bump)')
PY
