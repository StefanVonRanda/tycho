#!/usr/bin/env python3
"""Opt-in packing experiment. Production runtime and emitted language code stay put."""
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shlex
import statistics
import subprocess

from profile_runtime import instrument, once

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
OUT = ROOT / 'build/allocator-packing'
OUT.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
CC = shlex.split(os.environ.get('CC', 'cc'))
FLAGS = ['-O2', '-fwrapv', '-ffp-contract=off']
ENV = dict(os.environ, TYCHO_ARENA_STATS='0', TYCHO_CORELIB=str(ROOT / 'corelib'))
ENV.pop('TYCHO_BLOCK', None)


def run(cmd, label, env=ENV, expected=0, diagnostic=None):
    p = subprocess.run(cmd, env=env, text=True, capture_output=True, timeout=180)
    (OUT / (label + '.stdout')).write_text(p.stdout)
    (OUT / (label + '.stderr')).write_text(p.stderr)
    if p.returncode != expected or (diagnostic and diagnostic not in p.stderr):
        raise RuntimeError(f'{label}: exit {p.returncode}, expected {expected}: {p.stderr[-2000:]}')
    return p


def packed(src):
    src = once(src, 'size_t cap; size_t off; };',
               'size_t cap; size_t off; struct PackSlab *backing; };')
    backend = Path('bench/allocator/packed.c').read_text()
    backend += '''
static void pack_dump(void) {
    fprintf(stderr, "packing-profile: %zu %zu %zu\\n",
        (size_t)atomic_load(&pack_peak_reserved), (size_t)atomic_load(&pack_reserved),
        (size_t)atomic_load(&pack_slabs));
}
'''
    src = once(src, 'static HBlock *block_get(size_t cap) {',
               backend + '\nstatic HBlock *block_get(size_t cap) {')
    start = src.index('    uint64_t t_os = g_arena_stats ? st_now_ns() : 0;', src.index('static HBlock *block_get'))
    end = src.index('    b->off = 0;', start)
    src = src[:start] + '    HBlock *b = pack_block_new(cap);\n' + src[end:]
    src = once(src, '    while (g_block_pool) { HBlock *b = g_block_pool; g_block_pool = b->next; free(b); }',
               '    pack_cursor_drop();\n    while (g_block_pool) { HBlock *b = g_block_pool; g_block_pool = b->next; pack_block_free(b); }')
    src = once(src, 'static void stats_dump(void) {',
               'static void pack_dump(void);\nstatic void stats_dump(void) {\n    pack_dump();')
    # This counter now includes views carved from already-reserved backing,
    # not just pooled HBlocks. Do not keep the production report's pool claim.
    src = once(src, 'requests from pool (%.1f%%)', 'requests without fresh backing (%.1f%%)')
    return src


runtime = Path('runtime/tycho_rt.c').read_text()
candidate = packed(runtime)
rt = OUT / 'runtime'
rt.mkdir(exist_ok=True)
(rt / 'tycho_rt.c').write_text(candidate)


def contract(cc, name, flags):
    exe = OUT / ('contract-' + name)
    run(cc + flags + ['-fwrapv', '-ffp-contract=off', '-I', str(rt),
        'bench/allocator/packed_contract.c', '-lm', '-pthread', '-o', str(exe)], 'build-' + name)
    for stats in ('0', 'full'):
        env = dict(ENV, TYCHO_ARENA_STATS=stats, UBSAN_OPTIONS='halt_on_error=1',
                   ASAN_OPTIONS='detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'))
        p = run([str(exe)], name + '-' + stats, env)
        if 'packed-contract: disjoint ranges, partial flush and cross-thread release hold\n' not in p.stdout:
            raise RuntimeError('contract did not finish: ' + name)
    for mode in ('overflow', 'slab-overflow'):
        run([str(exe), mode], name + '-' + mode, expected=1, diagnostic='tycho: out of memory\n')


contract(CC, 'native', ['-O2'])
if os.environ.get('TYCHO_NO_ASAN') != '1':
    contract(CC, 'sanitized', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
else:
    print('packed-contract: sanitizer SKIPPED (TYCHO_NO_ASAN requested)')
# Probe the toolchain, not the candidate, so a candidate's ILP32 build failure
# cannot be misreported as a missing platform.
probe = OUT / 'probe.c'
probe.write_text('#include <stdatomic.h>\n#include <pthread.h>\nint main(void) { return 0; }\n')
p = subprocess.run(CC + ['-m32', str(probe), '-lm', '-pthread', '-o', str(OUT / 'probe')], capture_output=True)
(OUT / 'ilp32-probe.stderr').write_bytes(p.stderr)
if p.returncode == 0:
    contract(CC + ['-m32'], 'ilp32', ['-O2'])
else:
    print('packed-contract: ILP32 SKIPPED (toolchain probe failed; see log)')

# Prove shared storage does not justify widening ownership to the entire slab.
wrong = once(candidate,
    'if ((const char *)p >= base && (const char *)p < base + b->cap) return 1;',
    'if ((uintptr_t)p >= (uintptr_t)(b->backing + 1) && (uintptr_t)p < (uintptr_t)(b->backing + 1) + b->backing->cap) return 1;')
bad = OUT / 'foreign'
bad.mkdir(exist_ok=True)
(bad / 'tycho_rt.c').write_text(wrong)
run(CC + FLAGS + ['-I', str(bad), 'bench/allocator/packed_contract.c', '-lm', '-pthread',
    '-o', str(OUT / 'foreign-control')], 'build-foreign-control')
run([str(OUT / 'foreign-control')], 'foreign-control', expected=1,
    diagnostic='allocator-contract: child claims parent allocation\n')
leaked = once(candidate, 'static void pack_block_free(HBlock *b) { pack_slab_drop(b->backing); }',
              'static void pack_block_free(HBlock *b) { (void)b; }')
(rt / 'tycho_rt.c').write_text(leaked)
run(CC + FLAGS + ['-I', str(rt), 'bench/allocator/packed_contract.c', '-lm', '-pthread',
    '-o', str(OUT / 'leak-control')], 'build-leak-control')
run([str(OUT / 'leak-control')], 'leak-control', dict(ENV, TYCHO_ARENA_STATS='full'), expected=1,
    diagnostic='allocator-contract: backing leaked after partial flush\n')
(rt / 'tycho_rt.c').write_text(candidate)
print('packed-contract: native, available sanitizers/ILP32, overflow and both controls pass')

# Real generated-code callers, under both compilers, including implicit task
# finalization and caller-directed return/inout allocation. Compare locked stdout.
fixtures = ['value_semantics', 'inout_string', 'loop_return', 'or_return_frees',
            'or_else_frees', 'elem_recycle', 'projections', 'result_void', 'bounded_elems',
            'conc/implicit', 'conc/heap', 'conc/chan']
for compiler in ('./tychoc', './tychoc1'):
    for fixture in fixtures:
        label = Path(compiler).name + '-' + fixture.replace('/', '-')
        entry = 'tests/' + fixture + '.ty'
        dest = OUT / label
        run([compiler, entry, '--emit-c', '-o', str(dest)], label + '-emit')
        path = dest.with_suffix('.c')
        path.write_text(once(path.read_text(), runtime, candidate))
        shims = run([compiler, entry, '--print-shims'], label + '-shims').stdout.splitlines()
        modes = [('plain', FLAGS)]
        if os.environ.get('TYCHO_NO_ASAN') != '1':
            modes.append(('sanitized', ['-O1', '-g', '-fwrapv', '-ffp-contract=off',
                                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer']))
        for mode, flags in modes:
            exe = OUT / (label + '-' + mode)
            run(CC + flags + [str(path)] + shims + ['-lm', '-pthread', '-o', str(exe)], label + '-build-' + mode)
            env = dict(ENV, UBSAN_OPTIONS='halt_on_error=1',
                       ASAN_OPTIONS='detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'))
            p = run([str(exe)], label + '-run-' + mode, env)
            if p.stdout != Path('tests/' + fixture + '.out').read_text():
                raise RuntimeError('golden mismatch: ' + label + '-' + mode)
print(f'packed-semantics: {len(fixtures)} fixtures, both compilers, native and available sanitizer modes pass')

# Calibrate the shared profiler against exact logical-view totals too.
calibration = instrument(candidate) + '''
int main(void) {
    Arena a = arena_new(256);
    memset(arena_alloc(&a, 24), 1, 24);
    if (prof_owned != 256 || prof_peak_slack != 232 || atomic_load(&st_live) != 24) abort();
    arena_reset(&a);
    if (prof_owned != 256 || prof_peak_slack != 256 || atomic_load(&st_live) != 0) abort();
    arena_free(&a);
    if (prof_owned != 0 || prof_blocks != 0) abort();
    tycho_pool_flush();
    if (atomic_load(&pack_reserved) != 0) abort();
    return 0;
}
'''
(OUT / 'calibration.c').write_text(calibration)
run(CC + FLAGS + [str(OUT / 'calibration.c'), '-lm', '-pthread', '-o', str(OUT / 'calibration')], 'build-calibration')
run([str(OUT / 'calibration')], 'calibration', dict(ENV, TYCHO_ARENA_STATS='full'))

# The baseline target already calibrated native instrumentation and emitted this
# compiler. Require the baseline to match the current tree, not a stale binary.
base = json.loads(Path('build/allocator-baseline/results.json').read_text())
fingerprint = subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip()
if base['source_fingerprint'] != fingerprint:
    raise RuntimeError('stale baseline; run make allocator-packing')
src = Path('build/allocator-baseline/compiler.c').read_text()
if hashlib.sha256(src.encode()).hexdigest() != base['compiler_c_sha256']:
    raise RuntimeError('baseline emitted C changed')
packed_src = once(src, runtime, candidate)
shims = base['shims']
for variant, source in [('native', src), ('packed', packed_src)]:
    for kind, code in [('plain', source), ('profile', instrument(source))]:
        name = variant + '-' + kind
        path = OUT / (name + '.c')
        path.write_text(code)
        run(CC + FLAGS + [str(path)] + shims + ['-lm', '-pthread', '-o', str(OUT / name)], 'build-' + name)
run(CC + ['-O2', 'bench/peakrss.c', '-o', str(OUT / 'peakrss')], 'build-meter')

# Interleave plain runs; never compare native timing against instrumented code.
variants = [('default', 'native', None), ('small', 'native', '4096'), ('packed', 'packed', '4096')]
rows = {name: {'samples': []} for name, _, _ in variants}
for round_ in range(6):  # warmup, then five rounds (reverse alternate orders)
    for name, binary, block in (variants if round_ % 2 == 0 else list(reversed(variants))):
        env = dict(ENV)
        if block:
            env['TYCHO_BLOCK'] = block
        label = f'{name}-{round_}'
        p = run([str(OUT / 'peakrss'), str(OUT / (binary + '-plain')), 'compiler/main.ty',
                 '--emit-c', '-o', str(OUT / label)], label, env)
        digest = hashlib.sha256((OUT / (label + '.c')).read_bytes()).hexdigest()
        if digest != base['output_c_sha256']:
            raise RuntimeError('compiler output changed: ' + label)
        rss, wall = map(int, p.stderr.splitlines()[-1].split())
        if platform.system() == 'Darwin':
            rss //= 1024
        if round_:
            rows[name]['samples'].append({'wall_ms': wall, 'peak_rss_KiB': rss})

for name, binary, block in variants:
    env = dict(ENV, TYCHO_ARENA_STATS='full')
    if block:
        env['TYCHO_BLOCK'] = block
    label = name + '-profile'
    p = run([str(OUT / (binary + '-profile')), 'compiler/main.ty', '--emit-c', '-o', str(OUT / label)], label, env)
    if hashlib.sha256((OUT / (label + '.c')).read_bytes()).hexdigest() != base['output_c_sha256']:
        raise RuntimeError('profile output changed: ' + name)
    match = re.search(r'^allocator-profile: ((?:\d+ ){5}\d+)$', p.stderr, re.M)
    if not match:
        raise RuntimeError('missing profile: ' + name)
    values = list(map(int, match[1].split()))
    if values[-1] != 0:
        raise RuntimeError('logical ownership not released: ' + name)
    rows[name]['profile'] = dict(zip(['active_capacity_peak_bytes', 'active_tail_slack_peak_bytes',
        'capacity_at_slack_peak_bytes', 'bump_offsets_at_slack_peak_bytes',
        'active_blocks_peak', 'active_capacity_final_bytes'], values))
    if binary == 'packed':
        match = re.search(r'^packing-profile: (\d+) (\d+) (\d+)$', p.stderr, re.M)
        if not match:
            raise RuntimeError('missing backing profile')
        rows[name]['backing'] = dict(zip(['reserved_peak_bytes', 'reserved_final_bytes', 'slabs_created'], map(int, match.groups())))
    rows[name]['median_wall_ms'] = statistics.median(s['wall_ms'] for s in rows[name]['samples'])
    rows[name]['median_peak_rss_KiB'] = statistics.median(s['peak_rss_KiB'] for s in rows[name]['samples'])
report = {'source_fingerprint': fingerprint, 'platform': platform.platform(), 'cc': CC, 'cflags': FLAGS,
          'output_c_sha256': base['output_c_sha256'], 'variants': rows,
          'limits': ['Single compiler workload, not service latency or a bounded-budget caller.',
                     'Compare packing to small as well as default: smaller views alone can change slack.',
                     'Logical tail slack excludes unassigned slab tails, headers and pooled views.',
                     'Plain samples exclude profiling counters; backing references and larger headers are intrinsic to this prototype.',
                     'Prototype supports arbitrary lifetime order; it is not a two-owner double stack.',
                     'No production allocator or compiler-copy changes; no automatic promotion.']}
(OUT / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
print('Raw logs and emitted sources:', OUT)
