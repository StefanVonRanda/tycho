#!/bin/sh
# Semantic barriers and bounded retained-field allocation, with a defeated control.
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
env = dict(os.environ, TYCHO_CORELIB=str(root / 'corelib'), TYCHO_THREADS='2')
cc = shlex.split(os.environ.get('CC', 'cc'))
source = '''struct Payload:
    data: [int]
struct Record:
    payload: Payload
    tick: int
fn identity(n: int) -> int:
    return n
fn main():
    data := []int
    for i := 0; i < 2048; i += 1:
        push(data, i)
    r := Record(Payload(data), 0)
    saved := r
    for i := 0; i < COUNT; i += 1:
        r = Record(r.payload, STEP)
    r.payload.data[0] = 99
    if saved.payload.data[0] != 0 or r.tick != COUNT:
        die("retained field or snapshot changed")
    println("allocation ok")
'''


def run(compiler, label, text, expected):
    work = tmp / f'{compiler}-{label}'
    work.mkdir()
    stem = work / 'main'
    src = stem.with_suffix('.ty')
    src.write_text(text)
    built = subprocess.run([str(root / compiler), str(src), '--emit-c', '-o', str(stem)],
                           env=env, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stderr
    subprocess.run(cc + ['-O2', '-fwrapv', str(stem.with_suffix('.c')), '-lm',
                         '-pthread', '-o', str(stem)], check=True, timeout=60)
    result = subprocess.run([str(stem)], env=dict(env, TYCHO_ARENA_STATS='full'),
                            check=True, capture_output=True, text=True, timeout=30)
    assert result.stdout == expected, result.stdout
    row = re.search(r'bump-alloc:\s+([\d.]+) (B|KiB|MiB|GiB) over (\d+) allocations',
                    result.stderr)
    assert row, result.stderr
    return float(row[1]) * {'B': 1, 'KiB': 1024, 'MiB': 1024**2, 'GiB': 1024**3}[row[2]]


nested_source = source.replace('    data: [int]\n', '    data: [int]\n    count: int\n')
nested_source = nested_source.replace('Payload(data)', 'Payload(data, 0)').replace(
    'Record(r.payload, STEP)', 'Record(Payload(r.payload.data, r.payload.count + 1), STEP)')
nested_source = nested_source.replace('r.tick != COUNT:',
    'r.tick != COUNT or r.payload.count != COUNT or saved.payload.count != 0:')
generic_source = nested_source.replace('struct Payload:', 'struct Payload($T):').replace(
    '    data: [int]', '    data: $T').replace('struct Record:', 'struct Record($T):').replace(
    '    payload: Payload', '    payload: Payload($T)')

parts = tmp / 'parts'
parts.mkdir()
parts.joinpath('parts.ty').write_text('package parts\n\nstruct Payload:\n    data: [int]\n    count: int\n')
qualified_source = 'package main\nimport "../parts"\n' + nested_source[nested_source.index('struct Record:'):]
qualified_source = qualified_source.replace('payload: Payload', 'payload: parts.Payload').replace(
    'Payload(', 'parts.Payload(')

for compiler in ('tychoc', 'tychoc1'):
    run(compiler, 'semantics', (root / 'tests/fixtures/field_rebuild.src').read_text(),
        'field rebuild ok\n')
    for count in (64, 1024):
        text = source.replace('COUNT', str(count)).replace('STEP', 'r.tick + 1')
        bump = run(compiler, str(count), text, 'allocation ok\n')
        assert bump < 128 * 1024, f'{compiler}: {count} reconstructions used {bump} B'
        print(f'field-rebuild: {compiler}, {count} reconstructions: {bump:.0f} B bump')
    for label, template, count in (('nested-64', nested_source, 64),
                                   ('nested-1024', nested_source, 1024),
                                   ('generic-nested', generic_source, 1024),
                                   ('qualified-nested', qualified_source, 1024)):
        text = template.replace('COUNT', str(count)).replace('STEP', 'r.tick + 1')
        bump = run(compiler, label, text, 'allocation ok\n')
        assert bump < 128 * 1024, f'{compiler} {label}: used {bump} B'
        print(f'field-rebuild: {compiler}, {label}: {bump / 1024:.1f} KiB bump')
    # An unanalysed call keeps the ordinary copy path, despite identical results.
    text = source.replace('COUNT', '256').replace('STEP', 'identity(r.tick + 1)')
    bump = run(compiler, 'control', text, 'allocation ok\n')
    assert bump > 1024**2, f'{compiler}: call control unexpectedly passed ({bump} B)'
    print(f'field-rebuild: {compiler}, call control rejected: {bump:.0f} B bump')
    # Even a call buried in a nested constructor must defeat the whole proof.
    text = nested_source.replace('COUNT', '256').replace('STEP', 'r.tick + 1').replace(
        'r.payload.count + 1', 'identity(r.payload.count + 1)')
    bump = run(compiler, 'nested-call-control', text, 'allocation ok\n')
    assert bump > 1024**2, f'{compiler}: nested call control passed ({bump} B)'
    print(f'field-rebuild: {compiler}, nested call control rejected: {bump / 1024**2:.1f} MiB bump')
PY
