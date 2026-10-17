#!/bin/sh
# Return only the selected field's storage, not its temporary siblings.
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


def emit(compiler, label, text):
    work = tmp / f'{compiler}-{label}'
    work.mkdir()
    stem = work / 'main'
    stem.with_suffix('.ty').write_text(text)
    built = subprocess.run([str(root / compiler), str(stem.with_suffix('.ty')),
                            '--emit-c', '-o', str(stem)], env=env,
                           capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stderr
    return stem


def run(stem):
    subprocess.run(cc + ['-O2', '-fwrapv', str(stem.with_suffix('.c')), '-lm',
                         '-pthread', '-o', str(stem)], check=True, timeout=60)
    result = subprocess.run([str(stem)], env=dict(env, TYCHO_ARENA_STATS='full'),
                            check=True, capture_output=True, text=True, timeout=30)
    return result


def metrics(result):
    occupied_row = re.fullmatch(r'caller occupied: (\d+)\n', result.stdout)
    assert occupied_row, result.stdout
    row = re.search(r'bump-alloc:\s+([\d.]+) (B|KiB|MiB|GiB)', result.stderr)
    assert row, result.stderr
    bump = float(row[1]) * {'B': 1, 'KiB': 1024, 'MiB': 1024**2, 'GiB': 1024**3}[row[2]]
    return int(occupied_row[1]), bump


payload = '[' + ','.join(map(str, range(2048))) + ']'
scratch = '[' + ','.join(['99'] * 4096) + ']'
source = '''struct Inner:
    data: [int]
struct Pair:
    keep: [int]
    discard: [int]
struct Outer:
    inner: Inner
    discard: [int]
struct Generic($T):
    keep: $T
    discard: [int]
fn borrow_count(n: inout int):
    n += 0
fn produce(src: [int]) -> [int]:
    if len(src) != 2048:
        die("builder input changed")
    return PAYLOAD
fn projected() -> [int]:
    INIT
    return SELECT
fn main():
    a := projected()
    println(str(len(a)))
'''
# This harness measures occupied caller offsets, not capacity or RSS. Churning
# a child arena also makes a mistakenly returned scope-local buffer observable.
harness = r'''
#include <assert.h>
int main(void) {
    tycho_hash_seed_init();
    Arena owner = arena_new(0);
    __auto_type value = h_projected(&owner);
    __typeof__(value) saved[64];
    saved[0] = value;
    for (int i = 1; i < 64; i++) saved[i] = h_projected(&owner);
    size_t occupied = 0;
    for (HBlock *b = owner.head; b; b = b->next) occupied += b->off;
    assert(value.len == 2048);
    Arena churn = arena_child(&owner);
    memset(arena_alloc(&churn, 65536), 0x5a, 65536);
    arena_free(&churn);
    for (int n = 0; n < 64; n++)
        for (tycho_int i = 0; i < saved[n].len; i++) assert(saved[n].data[i] == i);
    saved[0].data[0] = 999;
    assert(saved[1].data[0] == 0);
    printf("caller occupied: %zu\n", occupied);
    arena_free(&owner);
    return 0;
}
'''

for compiler in ('tychoc', 'tychoc1'):
    stem = emit(compiler, 'semantics', (root / 'tests/fixtures/return_projection.src').read_text())
    result = run(stem)
    assert result.stdout == 'discarded once\n' + 'branch condition\n' * 4 + 'return projection ok\n', result.stdout
    for label, init, selected in (
        ('flat', f'r := Pair({payload}, {scratch})', 'r.keep'),
        ('nested', f'r := Outer(Inner({payload}), {scratch})', 'r.inner.data'),
        ('named', f'r := Pair(discard: {scratch}, keep: {payload})', 'r.keep'),
        ('generic', f'r := Generic({payload}, {scratch})', 'r.keep'),
        ('builder', f'r := Pair({payload}, {scratch})\n    r.keep = produce(r.keep)\n    r.keep = produce(r.keep)', 'r.keep'),
        ('nested-builder', f'r := Outer(Inner({payload}), {scratch})\n    r.inner.data = produce(r.inner.data)\n    r.inner.data = produce(r.inner.data)', 'r.inner.data'),
        ('generic-builder', f'r := Generic({payload}, {scratch})\n    r.keep = produce(r.keep)\n    r.keep = produce(r.keep)', 'r.keep'),
        ('loop', f'r := Pair({payload}, {scratch})\n    for i := 0; i < 4; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep'),
        ('peeled-loop', f'r := Pair({payload}, {scratch})\n    for i := 0; i < 3; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)\n    r.keep = produce(r.keep)', 'produce(r.keep)'),
        ('single-loop', f'r := Pair({payload}, {scratch})\n    for i := 0; i < 1; i += 1:\n        r.keep = produce(r.keep)', 'r.keep'),
        ('nested-loop', f'r := Outer(Inner({payload}), {scratch})\n    for i := 0; i < 4; i += 1:\n        r.inner.data = produce(r.inner.data)', 'r.inner.data'),
        ('generic-loop', f'r := Generic({payload}, {scratch})\n    for i := 0; i < 4; i += 1:\n        r.keep = produce(r.keep)', 'r.keep'),
        ('zero-loop', f'r := Pair({payload}, {scratch})\n    for i := 0; i < 0; i += 1:\n        r.keep = produce(r.keep)', 'r.keep'),
        ('loop-control', f'r := Pair({payload}, {scratch})\n    for i := 0; i < 4; i += 1:\n        r.keep = produce(r.keep)\n        r = Pair(r.keep, r.discard)', 'r.keep'),
        ('control', f'r := Pair({payload}, {scratch})\n    r = Pair(r.keep, r.discard)', 'r.keep'),
    ):
        stem = emit(compiler, label, source.replace('INIT', init).replace('SELECT', selected).replace('PAYLOAD', payload))
        cfile = stem.with_suffix('.c')
        code = cfile.read_text()
        assert code.count('int main(') == 1
        cfile.write_text(code.replace('int main(', 'int original_main(', 1) + harness)
        result = run(stem)
        occupied, bump = metrics(result)
        # A return copy adds 1 MiB. Builder cases create two additional versions;
        # neither the initial nor the intermediate version may occupy the caller.
        bounds = {'loop': 11.5, 'peeled-loop': 11.5, 'single-loop': 4.5, 'nested-loop': 7.5, 'generic-loop': 7.5}
        bound = bounds.get(label, 5.5 if 'builder' in label else 3.5) * 1024**2
        if label in ('control', 'loop-control', 'zero-loop'):
            minimum = 7.5 if label == 'loop-control' else 3.5
            assert bump >= minimum * 1024**2, f'{compiler} {label}: fallback not exercised'
        else:
            assert occupied == 1024**2, f'{compiler} {label}: retained siblings ({occupied} B)'
            assert bump < bound, f'{compiler} {label}: extra copy ({bump} B)'
        print(f'return-projection: {compiler} {label}: {occupied / 1024**2:.1f} MiB caller occupied; {bump / 1024**2:.1f} MiB bump')
    # Runtime value-parameter bounds: only the final write may occupy caller
    # storage. The mixed case includes INT64_MIN, zero, one and four iterations.
    for label, init, selected, count, bound in (
        ('dynamic', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', '4', 11.5),
        ('dynamic-mixed', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', None, 6.4),
        ('dynamic-nested', f'r := Outer(Inner({payload}), {scratch})\n    for i := 0; i < n; i += 1:\n        r.inner.data = produce(r.inner.data)', 'r.inner.data', '4', 7.5),
        ('dynamic-generic', f'r := Generic({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        r.keep = produce(r.keep)', 'r.keep', '4', 7.5),
        ('dynamic-peeled', f'r := Pair({payload}, {scratch})\n    if n <= 0:\n        return r.keep\n    for i := 0; i < n - 1; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)\n    r.keep = produce(r.keep)', 'produce(r.keep)', '4', 11.5),
        ('dynamic-inout-control', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', '&limit', 0),
        ('local', f'limit := n\n    r := Pair({payload}, {scratch})\n    for i := 0; i < limit; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', '4', 11.5),
        ('local-mixed', f'limit := n\n    r := Pair({payload}, {scratch})\n    for i := 0; i < limit; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', None, 6.4),
        ('local-length', f'items := [n,n,n,n]\n    limit := len(items)\n    items = [n]\n    r := Pair({payload}, {scratch})\n    for i := 0; i < limit; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', '4', 11.5),
        ('local-peeled', f'limit := n\n    r := Pair({payload}, {scratch})\n    if limit <= 0:\n        return r.keep\n    for i := 0; i < limit - 1; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)\n    r.keep = produce(r.keep)', 'produce(r.keep)', '4', 11.5),
        ('local-rebind-control', f'limit := n\n    limit = n\n    r := Pair({payload}, {scratch})\n    for i := 0; i < limit; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', '4', 0),
        ('local-borrow-control', f'limit := n\n    borrow_count(&limit)\n    r := Pair({payload}, {scratch})\n    for i := 0; i < limit; i += 1:\n        r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', '4', 0),
        ('branch', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        if i % 2 == 0:\n            r.keep = produce(r.keep)\n            r.keep = produce(r.keep)\n        else:\n            r.keep = produce(r.keep)\n            r.keep = produce(r.keep)', 'r.keep', '4', 11.5),
        ('branch-mixed', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        if i % 2 == 0:\n            r.keep = produce(r.keep)\n            r.keep = produce(r.keep)\n        else:\n            r.keep = produce(r.keep)\n            r.keep = produce(r.keep)', 'r.keep', None, 6.4),
        ('branch-followed', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        if i % 2 == 0:\n            r.keep = produce(r.keep)\n        else:\n            r.keep = produce(r.keep)\n        r.keep = produce(r.keep)', 'r.keep', '4', 11.5),
        ('branch-two', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        if i % 2 == 0:\n            r.keep = produce(r.keep)\n        else:\n            r.keep = produce(r.keep)\n        if i < 2:\n            r.keep = produce(r.keep)\n        else:\n            r.keep = produce(r.keep)', 'r.keep', '4', 11.5),
        ('branch-asymmetric', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        if i % 2 == 0:\n            r.keep = produce(r.keep)\n            r.keep = produce(r.keep)\n        else:\n            r.keep = produce(r.keep)', 'r.keep', '4', 9.5),
        ('branch-peeled', f'r := Pair({payload}, {scratch})\n    if n <= 0:\n        return r.keep\n    for i := 0; i < n - 1; i += 1:\n        if i % 2 == 0:\n            r.keep = produce(r.keep)\n            r.keep = produce(r.keep)\n        else:\n            r.keep = produce(r.keep)\n    if n % 2 == 1:\n        r.keep = produce(r.keep)', 'produce(r.keep)', '4', 9.5),
        ('branch-skipped-control', f'r := Pair({payload}, {scratch})\n    for i := 0; i < n; i += 1:\n        if i < n - 1:\n            r.keep = produce(r.keep)\n            r.keep = produce(r.keep)\n            r.keep = produce(r.keep)', 'r.keep', '4', 0),
    ):
        text = source.replace('INIT', init).replace('SELECT', selected).replace('PAYLOAD', payload)
        text = text.replace('fn projected()', 'fn projected(n: int)').replace('a := projected()', 'a := projected(4)')
        if label == 'dynamic-inout-control':
            text = text.replace('fn projected(n: int)', 'fn projected(n: inout int)').replace('a := projected(4)', 'limit := 4\n    a := projected(&limit)')
        stem = emit(compiler, label, text)
        if count is None:
            first, later = 'counts[0]', 'counts[i % 4]'
        else:
            first = later = count
        run_harness = harness.replace('h_projected(&owner);', f'h_projected(&owner, {first});', 1)
        run_harness = run_harness.replace('saved[i] = h_projected(&owner);', f'saved[i] = h_projected(&owner, {later});')
        run_harness = run_harness.replace('    Arena owner =', '    tycho_int limit = 4;\n    tycho_int counts[] = {INT64_MIN, 0, 1, 4};\n    Arena owner =')
        cfile = stem.with_suffix('.c')
        cfile.write_text(cfile.read_text().replace('int main(', 'int original_main(', 1) + run_harness)
        occupied, bump = metrics(run(stem))
        assert occupied == 1024**2, f'{compiler} {label}: retained versions ({occupied} B)'
        if bound:
            assert bump < bound * 1024**2, f'{compiler} {label}: extra copy ({bump} B)'
        else:
            assert bump > 11.5 * 1024**2, f'{compiler} {label}: fallback not exercised'
        print(f'return-projection: {compiler} {label}: {occupied / 1024**2:.1f} MiB caller occupied; {bump / 1024**2:.1f} MiB bump')
PY
