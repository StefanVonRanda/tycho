#!/usr/bin/env python3
"""Allocator baseline on real self-hosting work; never edits the runtime.

Timing/RSS use an uninstrumented compiler. A separate emitted-C copy measures
active block-tail slack, not semantic garbage or pooled/resident memory. The
fixed workload (compiler/main.ty --emit-c) is single-threaded. This is not a
bounded-budget caller and does not by itself justify a double-ended allocator.
"""
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

from profile_runtime import instrument

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
OUT = ROOT / 'build/allocator-baseline'
OUT.mkdir(parents=True, exist_ok=True)
# The defeated-accounting control aborts intentionally; do not dump its memory.
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
CC = shlex.split(os.environ.get('CC', 'cc'))
COMPILER = os.environ.get('TYCHOC', './tychoc1')
ENV = dict(os.environ)
ENV.pop('TYCHO_BLOCK', None)  # compare the runtime default, not a shell override
ENV['TYCHO_ARENA_STATS'] = '0'
ENV['TYCHO_CORELIB'] = str(ROOT / 'corelib')


def run(cmd, label, env=ENV):
    p = subprocess.run(cmd, env=env, text=True, capture_output=True, timeout=180)
    (OUT / (label + '.stdout')).write_text(p.stdout)
    (OUT / (label + '.stderr')).write_text(p.stderr)
    p.check_returncode()
    return p


# Calibrate against exact known block/offset totals, then defeat accounting in
# a copy and require the calibration to notice. No guessed subtraction of RSS
# and unrelated peaks is used as a slack measurement.
flags = ['-O2', '-fwrapv', '-ffp-contract=off']
calibration = instrument(Path('runtime/tycho_rt.c').read_text()) + '''
int main(void) {
    Arena a = arena_new(256);
    memset(arena_alloc(&a, 24), 1, 24);
    if (prof_owned != 256 || prof_peak_slack != 232 || atomic_load(&st_live) != 24) abort();
    arena_reset(&a);
    if (prof_owned != 256 || prof_peak_slack != 256 || atomic_load(&st_live) != 0) abort();
    arena_free(&a);
    if (prof_owned != 0 || prof_blocks != 0) abort();
    tycho_pool_flush();
    puts("allocator-profile calibration: ok");
    return 0;
}
'''
(OUT / 'calibration.c').write_text(calibration)
run(CC + flags + [str(OUT / 'calibration.c'), '-lm', '-pthread', '-o', str(OUT / 'calibration')], 'build-calibration')
measured_env = dict(ENV, TYCHO_ARENA_STATS='full')
p = run([str(OUT / 'calibration')], 'calibration', measured_env)
if p.stdout != 'allocator-profile calibration: ok\n':
    raise RuntimeError('calibration did not reach its checks')
(OUT / 'defeated.c').write_text(calibration.replace('prof_owned += b->cap; prof_blocks++;', ''))
run(CC + flags + [str(OUT / 'defeated.c'), '-lm', '-pthread', '-o', str(OUT / 'defeated')], 'build-defeated')
p = subprocess.run([str(OUT / 'defeated')], env=measured_env, capture_output=True, text=True)
if p.returncode == 0 or 'allocator-profile: inconsistent counters' not in p.stderr:
    raise RuntimeError('defeated accounting was not rejected')
print('allocator-profile: exact calibration passes; defeated accounting correctly rejected')

# Build two otherwise identical compilers from the current self-hosted output.
run([COMPILER, 'compiler/main.ty', '--emit-c', '-o', str(OUT / 'compiler')], 'emit')
src = (OUT / 'compiler.c').read_text()
(OUT / 'profile.c').write_text(instrument(src))
shims = run([COMPILER, 'compiler/main.ty', '--print-shims'], 'shims').stdout.splitlines()
for name, source in [('plain', 'compiler.c'), ('profile', 'profile.c')]:
    run(CC + flags + [str(OUT / source)] + shims + ['-lm', '-pthread', '-o', str(OUT / name)], 'build-' + name)
run(CC + ['-O2', 'bench/peakrss.c', '-o', str(OUT / 'peakrss')], 'build-meter')

expected = None
samples = []
for i, kind in enumerate(['warmup', 'timed', 'timed', 'timed', 'stats', 'profile']):
    name = 'sample-' + str(i)
    binary = OUT / ('profile' if kind == 'profile' else 'plain')
    env = dict(ENV, TYCHO_ARENA_STATS='full' if kind in ('stats', 'profile') else '0')
    p = run([str(OUT / 'peakrss'), str(binary), 'compiler/main.ty', '--emit-c',
             '-o', str(OUT / name)], name, env)
    output = OUT / (name + '.c')
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    if expected is None:
        expected = digest
    if digest != expected:
        raise RuntimeError('output changed in ' + kind)
    rss, wall = map(int, p.stderr.splitlines()[-1].split())
    if platform.system() == 'Darwin':
        rss //= 1024
    if kind == 'timed':
        samples.append({'wall_ms': wall, 'peak_rss_KiB': rss})
    if kind == 'stats':
        stats_text = p.stderr
    if kind == 'profile':
        match = re.search(r'^allocator-profile: (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)$', p.stderr, re.M)
        if not match:
            raise RuntimeError('profile report missing')
        values = list(map(int, match.groups()))
        if values[-1] != 0 or values[0] == 0 or values[1] == 0:
            raise RuntimeError('profile must see active blocks/slack and release all ownership')

report = {
    'workload': 'self-hosted compiler emitting compiler/main.ty (single-threaded)',
    'platform': platform.platform(), 'cc': CC, 'cflags': flags, 'shims': shims,
    'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'source_fingerprint': subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip(),
    'compiler_c_sha256': hashlib.sha256(src.encode()).hexdigest(),
    'output_c_sha256': expected, 'runtime_block_size': 'default (TYCHO_BLOCK removed)',
    'uninstrumented_samples': samples,
    'median_wall_ms': statistics.median(s['wall_ms'] for s in samples),
    'profile': dict(zip(['active_capacity_peak_bytes', 'active_tail_slack_peak_bytes',
                         'capacity_at_slack_peak_bytes', 'bump_offsets_at_slack_peak_bytes',
                         'active_blocks_peak', 'active_capacity_final_bytes'], values)),
    'unmodified_runtime_stats': stats_text,
    'limits': ['Timing/RSS exclude stats and added profiling counters.',
               'Cleanup time in runtime stats is aggregate instrumented time, not a tail bound.',
               'Tail slack excludes headers, recycle holes and pooled blocks; peaks are not simultaneous unless paired.',
               'OS reserved is cumulative allocator traffic, not RSS or semantic live bytes.',
               'No fixed-capacity caller or rewrite benefit has been demonstrated.'],
}
(OUT / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: v for k, v in report.items() if k != 'unmodified_runtime_stats'}, indent=2))
print('Raw logs and runtime stats:', OUT)
