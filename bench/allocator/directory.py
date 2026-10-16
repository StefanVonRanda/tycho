#!/usr/bin/env python3
"""Production list_dir packing contract; --bench compares native alternatives."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
try:
    import resource
except ImportError:  # native Windows has no POSIX resource module
    resource = None
import shlex
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
BASE = ROOT / 'build/dir-packing'
BASE.mkdir(parents=True, exist_ok=True)
OUT = Path(tempfile.mkdtemp(prefix='run-', dir=BASE))
CC = shlex.split(os.environ.get('CC', 'cc'))
ENV = dict(os.environ, TYCHO_ARENA_STATS='0')
ENV.pop('TYCHO_BLOCK', None)
if resource is not None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
bench = argparse.ArgumentParser(description=__doc__)
bench.add_argument('--bench', action='store_true')
options = bench.parse_args()


def run(cmd, label, env=ENV, expected=0, diagnostic=None):
    p = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=180)
    (OUT / (label + '.stdout')).write_text(p.stdout)
    (OUT / (label + '.stderr')).write_text(p.stderr)
    if p.returncode != expected or (diagnostic and diagnostic not in p.stderr):
        raise RuntimeError(f'{label}: exit {p.returncode}: {p.stderr[-1500:]}')
    return p


flags = ['-O2', '-fwrapv', '-ffp-contract=off']
source = 'tests/dir_packing.c'
run(CC + flags + ['-I', 'runtime', source, '-lm', '-pthread', '-o', str(OUT / 'native')], 'build-native')
legs = [('native', ENV)]
windows = os.name == 'nt' or platform.system().startswith(('MSYS', 'MINGW', 'CYGWIN'))
if os.environ.get('TYCHO_NO_ASAN') != '1' and not windows:
    run(CC + ['-O1', '-g', '-fwrapv', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I', 'runtime', source, '-lm', '-pthread', '-o', str(OUT / 'sanitized')], 'build-sanitized')
    legs.append(('sanitized', dict(ENV, UBSAN_OPTIONS='halt_on_error=1',
        ASAN_OPTIONS='detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'))))
else:
    print('dir-packing: sanitizers SKIPPED (TYCHO_NO_ASAN requested or Windows runtime unavailable)')
probe = OUT / 'probe.c'
probe.write_text('#include <pthread.h>\nint main(void){return 0;}\n')
p = subprocess.run(CC + ['-m32', str(probe), '-pthread', '-o', str(OUT / 'probe')], capture_output=True)
(OUT / 'probe.stderr').write_bytes(p.stderr)
if p.returncode == 0:
    run(CC + ['-m32'] + flags + ['-I', 'runtime', source, '-lm', '-pthread', '-o', str(OUT / 'ilp32')], 'build-ilp32')
    legs.append(('ilp32', ENV))
else:
    print('dir-packing: ILP32 SKIPPED (toolchain probe failed)')

with tempfile.TemporaryDirectory(prefix='tycho-dir-pack-') as tmp:
    root = Path(tmp)
    cases = [('empty', 0, 8), ('small', 8, 12), ('medium', 128, 32),
             ('dense', 700, 64), ('large', 1600, 200)]
    paths = []
    for label, count, length in cases:
        d = root / label
        d.mkdir()
        for i in range(count):
            name = f'{i:06}-' + chr(97 + i % 26) * (length - 7)
            (d / name).touch()
        paths.append((label, d))
    unicode_dir = root / 'unicode'
    unicode_dir.mkdir()
    names = ['é', '🙂', '.hidden', 'a space']
    if not windows:
        names.append('line\nbreak')
    for name in names:
        (unicode_dir / name).touch()
    paths += [('unicode', unicode_dir), ('missing', root / 'missing')]
    for name, env in legs:
        run([str(OUT / name), 'overflow'], name + '-overflow', env, expected=1,
            diagnostic='tycho: out of memory\n')
        for label, path in paths:
            for stats in ('0', 'full'):
                run([str(OUT / name), 'contract', str(path)], name + '-' + label + '-' + stats,
                    dict(env, TYCHO_ARENA_STATS=stats))
    print('dir-packing: native, available sanitizer/ILP32, tiny-block, collision, relocation, snapshot and fallback contracts pass')

    # Mutate only copies; an order or relocation defect must fail its own check.
    rt = Path('runtime/tycho_rt.c').read_text()
    mutations = {
        'order': ('for (tycho_int i = 0; i < b->len / 2; i++) {',
                  'for (tycho_int i = 0; i < 0; i++) {', 'entries/order differ'),
        'relocation': ('r.data[i] = out + (r.data[i] - base);',
                       'r.data[i] = r.data[i];', 'relocation broke name pointer'),
    }
    for name, (old, new, why) in mutations.items():
        if rt.count(old) != 1:
            raise RuntimeError('mutation anchor drift: ' + name)
        d = OUT / name
        d.mkdir(exist_ok=True)
        (d / 'tycho_rt.c').write_text(rt.replace(old, new))
        run(CC + flags + ['-I', str(d), source, '-lm', '-pthread', '-o', str(d / 'bad')], 'build-control-' + name)
        run([str(d / 'bad'), 'contract', str(paths[1][1])], 'control-' + name, expected=1,
            diagnostic='dir-packing: ' + why)
    print('dir-packing: defeated order and relocation guards correctly rejected')

    if options.bench:
        run(CC + ['-O2', 'bench/peakrss.c', '-o', str(OUT / 'peakrss')], 'build-meter')
        rows = []
        modes = ['original', 'fresh-native', 'stack-index', 'dual-tail']
        for label, path in paths[:5]:
            group = []
            checksums = set()
            for mode, name in enumerate(modes):
                args = [str(mode), str(path), '4']
                p = run([str(OUT / 'native')] + args, label + '-' + name + '-profile',
                        dict(ENV, TYCHO_ARENA_STATS='full'))
                result = json.loads(p.stdout)
                checksums.add(result['checksum'])
                group.append({'case': label, 'mode': name, 'reserved_bytes': result['reserved_bytes'],
                              'bump_bytes': result['bump_bytes'], 'checksum': result['checksum'], 'samples': []})
            if len(checksums) != 1:
                raise RuntimeError('benchmark output differs: ' + label)
            for round_ in range(6):
                order = list(enumerate(group))
                if round_ % 2:
                    order.reverse()
                for mode, row in order:
                    p = run([str(OUT / 'peakrss'), str(OUT / 'native'), str(mode), str(path), '4'],
                            label + '-' + row['mode'] + '-' + str(round_))
                    result = json.loads(p.stdout)
                    if result['checksum'] != row['checksum']:
                        raise RuntimeError('plain/profile mismatch')
                    rss, _ = map(int, p.stderr.splitlines()[-1].split())
                    if platform.system() == 'Darwin':
                        rss //= 1024
                    if round_:
                        row['samples'].append({'elapsed_ns': result['elapsed_ns'], 'peak_rss_KiB': rss})
            for row in group:
                row['median_ms'] = statistics.median(s['elapsed_ns'] for s in row['samples']) / 1e6
                row['median_rss_KiB'] = statistics.median(s['peak_rss_KiB'] for s in row['samples'])
                print(label, row['mode'], row['reserved_bytes'], row['bump_bytes'], row['median_ms'], row['median_rss_KiB'])
            rows += group
        report = {'cc': CC, 'cflags': flags, 'contexts': 32, 'rounds': 4, 'cases': cases, 'rows': rows,
                  'log_directory': str(OUT.relative_to(ROOT)),
                  'runtime_sha256': hashlib.sha256(rt.encode()).hexdigest(),
                  'source_fingerprint': subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip(),
                  'limits': ['Real directory enumeration on controlled fixtures, not end-to-end application latency.',
                             'Reservation/bump use separate stats-enabled runs; timing/RSS use stats-off.',
                             'Fresh-native separates known-fresh construction from the dual-tail effect.',
                             'Stack-index is a simpler bounded-512-pointer alternative; it falls back to native growth.']}
        text = json.dumps(report, indent=2) + '\n'
        (OUT / 'results.json').write_text(text)
        published = OUT / 'publish.json'
        published.write_text(text)
        os.replace(published, BASE / 'results.json')
    print('dir-packing: artifacts:', OUT)
