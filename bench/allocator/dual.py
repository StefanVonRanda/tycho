#!/usr/bin/env python3
"""Scenario matrix for actual opposing bump cursors; no runtime replacement."""
import hashlib
import json
from itertools import product
import os
from pathlib import Path
import platform
import resource
import shlex
import statistics
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
OUT = ROOT / 'build/allocator-dual'
OUT.mkdir(parents=True, exist_ok=True)
CC = shlex.split(os.environ.get('CC', 'cc'))
ENV = dict(os.environ, TYCHO_ARENA_STATS='0')
ENV.pop('TYCHO_BLOCK', None)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(cmd, label, env=ENV, expected=0, diagnostic=None):
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=180)
    (OUT / (label + '.stdout')).write_text(p.stdout)
    (OUT / (label + '.stderr')).write_text(p.stderr)
    if p.returncode != expected or (diagnostic and diagnostic not in p.stderr):
        raise RuntimeError(f'{label}: exit {p.returncode}: {p.stderr[-1500:]}')
    return p


flags = ['-O2', '-fwrapv', '-ffp-contract=off']
source = Path('bench/allocator/dual.c')
include = ['-I', 'runtime', '-I', 'bench/allocator']
run(CC + flags + include + [str(source), '-lm', '-pthread', '-o', str(OUT / 'plain')], 'build-plain')
run([str(OUT / 'plain'), 'contract'], 'contract')
sanitizers = os.environ.get('TYCHO_NO_ASAN') != '1'
if sanitizers:
    run(CC + ['-O1', '-g', '-fwrapv', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] + include +
        [str(source), '-lm', '-pthread', '-o', str(OUT / 'sanitized')], 'build-sanitized')
    sanenv = dict(ENV, UBSAN_OPTIONS='halt_on_error=1',
                  ASAN_OPTIONS='detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'))
    run([str(OUT / 'sanitized'), 'contract'], 'contract-sanitized', sanenv)
probe = OUT / 'probe.c'
probe.write_text('#include <pthread.h>\n#include <stdatomic.h>\nint main(void){return 0;}\n')
p = subprocess.run(CC + ['-m32', str(probe), '-pthread', '-o', str(OUT / 'probe')], capture_output=True)
(OUT / 'probe.stderr').write_bytes(p.stderr)
ilp32 = p.returncode == 0
if ilp32:
    run(CC + ['-m32'] + flags + include + [str(source), '-lm', '-pthread', '-o', str(OUT / 'ilp32')], 'build-ilp32')
    run([str(OUT / 'ilp32'), 'contract'], 'contract-ilp32')
else:
    print('dual: ILP32 SKIPPED (toolchain probe failed)')

# Deliberately remove guards in PRIVATE headers; the contract must identify the
# resulting fault, not merely crash or fail compilation.
header = Path('bench/allocator/dual.h').read_text()
mutations = {
    'collision': ('if (n > d->high - d->low) return NULL;',
                  'if (0) return NULL;', 'overflow accepted'),
    'order': ('m.token != d->top', '0', 'non-LIFO rewind accepted'),
    'rewind': ('d->high = m.high; d->top = m.parent;',
               'd->low = 0; d->high = d->cap; d->top = m.parent;', 'outer rewind corrupts survivors'),
}
# Rewind corruption is checked by cursors/ownership too, not just bytes that
# happened not to have been overwritten yet.
for name, (old, new, why) in mutations.items():
    if header.count(old) != 1:
        raise RuntimeError('mutation anchor drift: ' + name)
    d = OUT / name
    d.mkdir(exist_ok=True)
    (d / 'dual.h').write_text(header.replace(old, new))
    # Copy source too: quoted includes search the source directory first.
    (d / 'dual.c').write_text(source.read_text())
    run(CC + flags + ['-I', str(d), '-I', 'runtime'] + [str(d / 'dual.c'), '-lm', '-pthread',
        '-o', str(d / 'bad')], 'build-control-' + name)
    run([str(d / 'bad'), 'contract'], 'control-' + name, expected=1, diagnostic='dual-contract: ' + why)

# Repository-derived line lengths and byte content; this is not an end-to-end
# application speedup. Preserve the exact source list/hashes and packed corpus.
sources = ['compiler/main.ty', 'compiler/emit/emit.ty', 'tools/tycho-db/srv/srv.ty']
records = []
for name in sources:
    records += [line[:4096] for line in Path(name).read_bytes().splitlines() if line]
# Even coverage across the complete source sequence, bounded for the C loader.
if len(records) > 16000:
    records = [records[i * len(records) // 16000] for i in range(16000)]
corpus = b''.join(struct.pack('<I', len(line)) + line for line in records)
(OUT / 'corpus.bin').write_bytes(corpus)
run(CC + ['-O2', 'bench/peakrss.c', '-o', str(OUT / 'peakrss')], 'build-meter')
scenarios = ['index', 'scratch-result', 'nested-scratch', 'skew', 'collision', 'retained-growth', 'anti-correlated']
modes = ['native-separate', 'native-single', 'fixed-split', 'dual', 'native-small']
mode_codes = [0, 1, 2, 3, -1]
rows = []
for schedule, budget in product(('serial', 'held-results'), (32768, 65536, 73728, 131072)):
    for scenario, scenario_name in enumerate(scenarios):
        checksums = set()
        group = []
        for mode, mode_name in enumerate(modes):
            label = f'{schedule}-{budget}-{scenario_name}-{mode_name}'
            args = [str(mode_codes[mode]), str(scenario), str(OUT / 'corpus.bin'), str(budget), '32', '8',
                    '0' if schedule == 'serial' else '1']
            profile = run([str(OUT / 'plain')] + args, label + '-profile', dict(ENV, TYCHO_ARENA_STATS='full'))
            result = json.loads(profile.stdout)
            if result['fits']:
                checksums.add(result['checksum'])
            if mode_codes[mode] < 2 and not result['fits']:
                raise RuntimeError('growing native allocator rejected work')
            if mode == 3 and scenario in (3, 4, 5, 6):
                minimum = {3: 53248, 4: 81920, 5: 73728, 6: 53248}[scenario]
                if result['fits'] != (budget >= minimum):
                    raise RuntimeError('dual capacity boundary violated: ' + label)
            if ilp32:
                p = run([str(OUT / 'ilp32')] + args, label + '-ilp32')
                narrow = json.loads(p.stdout)
                if narrow['fits'] != result['fits'] or narrow['checksum'] != result['checksum']:
                    raise RuntimeError('ILP32 outcome differs: ' + label)
            if sanitizers:
                # Every scenario/budget/mode, including rejected capacities and
                # cleanup after partial construction, runs under sanitizers.
                p = run([str(OUT / 'sanitized')] + args, label + '-sanitized', sanenv)
                san = json.loads(p.stdout)
                if san['fits'] != result['fits'] or san['checksum'] != result['checksum']:
                    raise RuntimeError('sanitizer outcome differs: ' + label)
            group.append({'mode': mode_name, 'scenario': scenario_name, 'schedule': schedule, 'budget_per_context': budget,
                          'fits': result['fits'], 'reserved_bytes': result['reserved_bytes'],
                          'checksum': result['checksum'], 'samples': []})
        # Warmup plus five interleaved rounds; alternate order to reduce fixed
        # ordering bias. Rejected capacities are outcomes, not fast samples.
        for round_ in range(6):
            order = list(enumerate(group))
            if round_ % 2:
                order.reverse()
            for mode, row in order:
                if not row['fits']:
                    continue
                label = f'{schedule}-{budget}-{scenario_name}-{row["mode"]}-{round_}'
                args = [str(mode_codes[mode]), str(scenario), str(OUT / 'corpus.bin'), str(budget), '32', '8',
                        '0' if schedule == 'serial' else '1']
                p = run([str(OUT / 'peakrss'), str(OUT / 'plain')] + args, label)
                sample = json.loads(p.stdout)
                if not sample['fits'] or sample['checksum'] != row['checksum']:
                    raise RuntimeError('plain/profile outcome differs: ' + label)
                rss, _ = map(int, p.stderr.splitlines()[-1].split())
                if platform.system() == 'Darwin':
                    rss //= 1024
                if round_:
                    row['samples'].append({'wall_ns': sample['elapsed_ns'], 'cleanup_ns': sample['cleanup_ns'],
                                           'peak_rss_KiB': rss})
        for row in group:
            samples = row['samples']
            row['median_wall_ns'] = statistics.median(s['wall_ns'] for s in samples) if samples else None
            row['median_rss_KiB'] = statistics.median(s['peak_rss_KiB'] for s in samples) if samples else None
            row['maximum_observed_final_cleanup_ns'] = max(s['cleanup_ns'] for s in samples) if samples else None
        if len(checksums) != 1:
            raise RuntimeError('successful variants disagree: ' + scenario_name)
        rows += group
        print(schedule, budget, scenario_name, [(r['mode'], r['fits'], r['reserved_bytes']) for r in group], flush=True)
report = {'cc': CC, 'cflags': flags, 'platform': platform.platform(), 'contexts': 32, 'rounds': 8,
          'corpus_records': len(records), 'corpus_sha256': hashlib.sha256(corpus).hexdigest(),
          'sources': {s: hashlib.sha256(Path(s).read_bytes()).hexdigest() for s in sources},
          'source_fingerprint': subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip(),
          'contracts': {'native': True, 'sanitizers': sanitizers, 'ilp32': ilp32, 'negative_controls': list(mutations)},
          'rows': rows,
          'limits': ['Repository-data scenarios, not end-to-end application speedups.',
                     'Native arenas grow; fixed variants explicitly reject collision, never silently drop work.',
                     'Reserved native bytes come from a separate stats-enabled run; timings/RSS are stats-off.',
                     'Native single uses a presized index in index mode; otherwise it retains scratch until batch end.',
                     'Fixed buffers are allocated but only live payloads are touched; reservation is not RSS.',
                     'Timings include checking/copying/hashing and final release, not just allocator instructions.',
                     'Maximum cleanup is the largest observed final release of 32 contexts, not a tail-latency guarantee.',
                     'POD only; no arbitrary arena graph, cross-thread sharing or finalizer integration.']}
(OUT / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print('dual: contracts, controls, full scenario matrix and matching successful outputs pass; logs:', OUT)
