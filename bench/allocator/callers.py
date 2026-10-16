#!/usr/bin/env python3
"""Opt-in end-to-end directory workloads and actual HTTP/DB reader lifetime traces.

Never modifies production sources. All controls/instrumentation are emitted-C
copies. POSIX socketpairs exercise the real net shim without opening a listener.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import resource
import shlex
import shutil
import statistics
import subprocess
import tempfile

from profile_runtime import instrument, once

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--readers-only', action='store_true', help='skip directory application measurements')
options = parser.parse_args()
ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
BASE = ROOT / 'build/allocator-callers'
BASE.mkdir(parents=True, exist_ok=True)
OUT = Path(tempfile.mkdtemp(prefix='run-', dir=BASE))
CC = shlex.split(os.environ.get('CC', 'cc'))
ENV = dict(os.environ, TYCHO_ARENA_STATS='0', TYCHO_CORELIB=str(ROOT / 'corelib'))
ENV.pop('TYCHO_BLOCK', None)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
FLAGS = ['-O2', '-fwrapv', '-ffp-contract=off']
COMPILER = os.environ.get('TYCHOC', './tychoc1')
fingerprint = subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip()
report = {'cc': CC, 'flags': FLAGS, 'directory': [], 'readers': [], 'layout': [],
          'phase_codes': {'1': 'HTTP returned raw/body/rest', '2': 'DB returned line/rest',
                          '10': 'HTTP loop raw length', '20': 'DB loop buffer length',
                          '21': 'DB split raw/line/rest before rebinding', '22': 'DB return owner equals buffer owner'},
          'layout_fields': ['mode', 'fits', 'payload_capacity_B', 'required_payload_B'],
          'layout_modes': ['native-default', 'native-small', 'native-presized', 'dual-fixed', 'dual-exact']}


def run(cmd, label, env=ENV, ok=True):
    p = subprocess.run([str(c) for c in cmd], env=env, capture_output=True, text=True, timeout=240)
    (OUT / (label + '.stdout')).write_text(p.stdout)
    (OUT / (label + '.stderr')).write_text(p.stderr)
    if ok and p.returncode:
        raise RuntimeError(label + ': ' + p.stderr[-2000:])
    return p


def build(src, name, shims, flags=FLAGS):
    source = OUT / (name + '.c')
    source.write_text(src)
    binary = OUT / name
    run(CC + flags + [source] + shims + ['-lm', '-pthread', '-o', binary], 'build-' + name)
    return binary


def emit(source, name, compiler=COMPILER):
    dest = OUT / name
    run([compiler, source, '--emit-c', '-o', dest], 'emit-' + name)
    shims = run([compiler, source, '--print-shims'], 'shims-' + name).stdout.splitlines()
    deps = run([compiler, source, '--print-deps'], 'deps-' + name).stdout.splitlines()
    if deps:
        shims += shlex.split(run(['pkg-config', '--cflags', '--libs'] + deps, 'pkg-' + name).stdout)
    return dest.with_suffix('.c').read_text(), shims


def body(src, name):
    m = re.search(r'^\w[^\n]*\b' + re.escape(name) + r'\([^\n]*\) \{\n', src, re.M)
    if not m:
        raise RuntimeError('function definition missing: ' + name)
    return m.start(), src.index('\n}', m.end()) + 2


def profile(src):
    src = instrument(src)
    return once(src, 'static void prof_dump(void) {',
                'static void prof_dump(void) {\n    fprintf(stderr, "caller-os: %zu\\n", (size_t)atomic_load(&st_os_bytes));')


def counters(stderr):
    m = re.search(r'^allocator-profile: (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)$', stderr, re.M)
    if not m or int(m[6]):
        raise RuntimeError('missing profile or surviving arena ownership')
    return {'peak_active_capacity_B': int(m[1]), 'peak_tail_slack_B': int(m[2]),
            'active_capacity_final_B': int(m[6]),
            'cumulative_reserved_B': int(re.search(r'^caller-os: (\d+)$', stderr, re.M)[1])}


def clear_output(path):
    if OUT not in path.parents:
        raise RuntimeError('output must stay in this run directory')
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def output_digest(path):
    if path.is_dir():
        manifest = [(str(f.relative_to(path)), hashlib.sha256(f.read_bytes()).hexdigest())
                    for f in sorted(path.rglob('*')) if f.is_file()]
        return hashlib.sha256(json.dumps(manifest).encode()).hexdigest()
    return hashlib.sha256(path.read_bytes()).hexdigest()


rt = Path('runtime/tycho_rt.c').read_text()
# Calibrate actual ownership accounting, then demonstrate a defeated counter is caught.
cal = profile(rt) + '''
int main(void) {
    Arena a = arena_new(256);
    memset(arena_alloc(&a, 24), 1, 24);
    if (prof_owned != 256 || prof_peak_slack != 232 || atomic_load(&st_live) != 24) abort();
    arena_reset(&a);
    if (prof_owned != 256 || prof_peak_slack != 256 || atomic_load(&st_live) != 0) abort();
    arena_free(&a);
    if (prof_owned || prof_blocks) abort();
    tycho_pool_flush(); return 0;
}
'''
measured = dict(ENV, TYCHO_ARENA_STATS='full')
run([build(cal, 'calibration', [])], 'calibration', measured)
if cal.count('prof_owned += b->cap; prof_blocks++;') != 3:
    raise RuntimeError('calibration acquisition anchors changed')
bad = build(cal.replace('prof_owned += b->cap; prof_blocks++;', ''), 'defeated', [])
p = run([bad], 'defeated', measured, ok=False)
if p.returncode == 0 or 'allocator-profile: inconsistent counters' not in p.stderr:
    raise RuntimeError('defeated profile did not fail for its own reason')
run(CC + ['-O2', 'bench/peakrss.c', '-o', OUT / 'peakrss'], 'build-meter')

# Three existing applications, ordinary repository trees and a labelled wide
# filesystem fixture containing repository-derived bytes (not a synthetic walker).
wide = OUT / 'wide'
wide.mkdir()
payload = Path('compiler/main.ty').read_bytes()[:128]
for i in range(700):
    (wide / (f'{i:06}-' + chr(97 + i % 26) * 57)).write_bytes(payload)
for label, root in [('corelib', ROOT / 'corelib'), ('wide', wide)]:
    (OUT / (label + '.toml')).write_text('[snapshot]\nname = "caller-' + label + '"\nroot = ' + json.dumps(str(root)) + '\n')
apps = [] if options.readers_only else [('compiler', 'compiler/main.ty'),
        ('docs', 'tools/tycho-docs/main.ty'), ('snapshot', 'tools/tycho-snap/main.ty')]
controls = Path('tests/dir_packing.c').read_text()
controls = controls[controls.index('static void need'):controls.index('static TychoArrStr list(')]
modes = [('original', None), ('fresh-native', None), ('stack-index', None),
         ('fresh-small', '4096'), ('dual-tail', None)]
for app, source in apps:
    src, shims = emit(source, app)
    start, end = body(src, 'tycho_list_dir')
    variants = {}
    for mode, block in modes:
        replacement = {'original': 'original(a, path)', 'fresh-native': 'native(a, path, 0)',
                       'fresh-small': 'native(a, path, 0)', 'stack-index': 'native(a, path, 1)'}.get(mode)
        code = src if replacement is None else src[:start] + controls + '\nTychoArrStr tycho_list_dir(Arena *a, const char *path) {\n    return ' + replacement + ';\n}' + src[end:]
        plain = build(code, app + '-' + mode, shims)
        a, b = body(code, 'tycho_list_dir')
        impl = code[a:b].replace('tycho_list_dir(', 'caller_list_impl(', 1)
        wrapper = '''
TychoArrStr tycho_list_dir(Arena *a, const char *path) {
    size_t before_cap = prof_owned, before_bump = atomic_load(&st_live);
    TychoArrStr r = caller_list_impl(a, path);
    size_t bytes = 0;
    for (tycho_int i = 0; i < r.len; i++) bytes += (size_t)tycho_str_len(r.data[i]);
    fprintf(stderr, "directory: %lld %zu %zu %zu\\n", (long long)r.len, bytes,
        prof_owned - before_cap, (size_t)atomic_load(&st_live) - before_bump);
    return r;
}
'''
        traced = build(profile(code[:a] + impl + wrapper + code[b:]), app + '-' + mode + '-profile', shims)
        variants[mode] = (plain, traced, dict(ENV, **({'TYCHO_BLOCK': block} if block else {})))
    cases = [(app, ['compiler/main.ty', '--emit-c', '-o', str(OUT / 'output')], OUT / 'output.c')] if app == 'compiler' else (
        [(app, ['docs', str(OUT / 'site')], OUT / 'site')] if app == 'docs' else
        [('snapshot-' + label, ['--manifest=' + str(OUT / (label + '.toml')), '--out=' + str(OUT / 'output.zip'), '--quiet'], OUT / 'output.zip') for label in ('corelib', 'wide')])
    for case, args, output in cases:
        expected = None
        rows = []
        for mode, _ in modes:
            binary, traced, env = variants[mode]
            clear_output(output)
            p = run([traced] + args, case + '-' + mode + '-profile', dict(env, TYCHO_ARENA_STATS='full'))
            digest = output_digest(output)
            signature = (p.stdout, digest)
            if expected is None:
                expected = signature
            if signature != expected:
                raise RuntimeError('application output differs: ' + case + '/' + mode)
            entries = [list(map(int, m)) for m in re.findall(r'^directory: (\d+) (\d+) (\d+) (\d+)$', p.stderr, re.M)]
            rows.append({'case': case, 'mode': mode, **counters(p.stderr), 'directory_calls': entries,
                         'directory_call_fields': ['entries', 'name_bytes', 'added_active_capacity_B', 'added_bump_B'],
                         'output_sha256': digest, 'samples': []})
        for round_ in range(6):
            order = rows if round_ % 2 == 0 else list(reversed(rows))
            for row in order:
                binary, _, env = variants[row['mode']]
                clear_output(output)
                p = run([OUT / 'peakrss', binary] + args, case + '-' + row['mode'] + '-' + str(round_), env)
                if (p.stdout, output_digest(output)) != expected:
                    raise RuntimeError('plain/profile output mismatch')
                rss, ms = map(int, p.stderr.splitlines()[-1].split())
                if platform.system() == 'Darwin':
                    rss //= 1024
                if round_:
                    row['samples'].append({'wall_ms': ms, 'peak_rss_KiB': rss})
        for row in rows:
            row['median_wall_ms'] = statistics.median(s['wall_ms'] for s in row['samples'])
            row['median_rss_KiB'] = statistics.median(s['peak_rss_KiB'] for s in row['samples'])
            print(case, row['mode'], row['peak_active_capacity_B'], row['cumulative_reserved_B'], row['median_wall_ms'], row['median_rss_KiB'], flush=True)
        report['directory'] += rows

# Reader fixtures invoke the real production functions, their generated C and
# real net shim. Only recv request size is clamped, in the fixture wrapper.
reader_src, shims = emit('bench/allocator/readers.ty', 'readers')
net_entries = [s for s in shims if Path(s).resolve() == ROOT / 'corelib/net/net_shim.c']
if len(net_entries) != 1:
    raise RuntimeError('net shim resolution drift')
net = net_entries[0]
net_obj = OUT / 'net.o'
run(CC + FLAGS + ['-Dnetx_read=caller_real_read', '-c', net, '-o', net_obj], 'build-net')
links = [s for s in shims if s != net] + [net_obj, 'bench/allocator/reader_transport.c']
traced = reader_src
start, end = body(traced, 'h_httpd__read_request_resume')
part = traced[start:end]
part = once(part, 'while (h_reading) {', 'while (h_reading) {\n            caller_note(10, tycho_str_len(h_buf), 0, 0);')
traced = traced[:start] + part + traced[end:]
start, end = body(traced, 'h_srv__recv_line')
part = traced[start:end]
part = once(part, 'while (1) {', 'while (1) {\n            caller_note(20, tycho_str_len(*h_buf), 0, 0);')
# Match the fresh locals, not their generation-specific tuple temporary.
m = re.search(r'char \*h_rest = [^\n]+;', part)
if not m:
    raise RuntimeError('DB split locals changed')
part = part[:m.end()] + '\n                caller_note(21, tycho_str_len(*h_buf), tycho_str_len(h_line), tycho_str_len(h_rest));\n                caller_note(22, _parent == _ina_h_buf, 0, 0);' + part[m.end():]
traced = traced[:start] + part + traced[end:]
plain = build(reader_src, 'reader-plain', links)
probe = build(profile(traced), 'reader-profile', links)
san_flags = ['-O1', '-g', '-fwrapv', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
net_san = OUT / 'net-sanitized.o'
run(CC + san_flags + ['-Dnetx_read=caller_real_read', '-c', net, '-o', net_san], 'build-net-sanitized')
sanitized = build(reader_src, 'reader-sanitized',
                  [net_san if s == net_obj else s for s in links], san_flags)
# Bootstrap is a separate compiler-correctness check, not a timing control.
boot_src, boot_shims = emit('bench/allocator/readers.ty', 'reader-bootstrap', './tychoc')
if sorted(str(Path(s).resolve()) for s in boot_shims) != sorted(str(Path(s).resolve()) for s in shims):
    raise RuntimeError('compiler shim sets differ')
bootstrap = build(boot_src, 'reader-bootstrap-bin', links)
http = lambda body: b'POST /data HTTP/1.1\r\nHost: local\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body
cases = [('http-small', 'http', http(b'abc'), 'ok body=3\n'),
         ('http-binary', 'http', http(bytes(range(256)) * 256), 'ok body=65536\n'),
         ('http-near-cap', 'http', http(b'x' * 1048000), 'ok body=1048000\n'),
         ('http-short', 'http', b'POST / HTTP/1.1\r\nContent-Length: 9\r\n\r\nx', 'error Closed\n'),
         ('http-ambiguous', 'http', b'POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nx', 'error Malformed\n'),
         ('db-two', 'db', b'a' * 7000 + b'\r\nsecond\n', 'line=7000\nline=6\n'),
         ('db-short', 'db', b'unfinished', 'error connection lost: closed mid-statement\nerror connection lost: closed mid-statement\n'),
         ('db-over-cap', 'db', b'a' * 10000 + b'\nsecond\n', 'error statement longer than 8192 bytes\n' * 2),
         ('db-limit-lf', 'db', b'a' * 8192 + b'\nsecond\n', 'line=8192\nline=6\n'),
         ('db-limit-crlf', 'db', b'a' * 8192 + b'\r\nsecond\n', 'line=8192\nline=6\n'),
         ('db-over-lf', 'db', b'a' * 8193 + b'\nsecond\n', 'error statement longer than 8192 bytes\n' * 2),
         ('db-over-crlf', 'db', b'a' * 8193 + b'\r\nsecond\n', 'error statement longer than 8192 bytes\n' * 2),
         ('db-prefilled-pipeline', 'db-prefill', (b'a' * 8192 + b'\n') * 2, 'line=8192\n' * 2),
         ('db-empty-lines', 'db', b'\n\r\n', 'line=0\n' * 2),
         ('db-clean-eof', 'db', b'', 'eof\n' * 2),
         ('db-pending-cr', 'db', b'a' * 8192 + b'\r', 'error connection lost: closed mid-statement\n' * 2),
         ('db-byte-limit', 'db', 'é'.encode() * 4096 + b'\nsecond\n', 'line=8192\nline=6\n')]
for case, kind, data, expected in cases:
    payload_path = OUT / (case + '.data')
    payload_path.write_bytes(data)
    for chunk in (64, 4096):
        for block in ('65536', '4096'):
            args = [kind, payload_path, str(chunk)]
            name = case + '-' + str(chunk) + '-' + block
            env = dict(ENV, TYCHO_BLOCK=block)
            result = run([plain] + args, name + '-plain', env)
            if expected is not None and result.stdout != expected:
                raise RuntimeError(name + ': unexpected outcome ' + result.stdout)
            p = run([probe] + args, name + '-profile', dict(env, TYCHO_ARENA_STATS='full', CALLER_TRACE='1'))
            if result.stdout != p.stdout:
                raise RuntimeError('reader instrumentation changed output')
            points = [list(map(int, m)) for m in re.findall(r'^phase: (\d+) (\d+) (\d+) (\d+)$', p.stderr, re.M)]
            report['readers'].append({'case': case, 'chunk_limit': chunk, 'block': block,
                'stdout': result.stdout, **counters(p.stderr), 'phases': points})
            for variant, binary in [('sanitized', sanitized), ('bootstrap', bootstrap)]:
                checked = run([binary] + args, name + '-' + variant,
                              dict(env, ASAN_OPTIONS='detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'),
                                   UBSAN_OPTIONS='halt_on_error=1'))
                if checked.stdout != result.stdout:
                    raise RuntimeError('reader compiler/sanitizer output differs')
    print(case, 'real reader/default+small/native+sanitized/bootstrap+profile passed', flush=True)

# Defeat the actual pre-split check: the one-byte-over line must be admitted
# by this mutant, while every ordinary compiler/sanitizer path rejects it.
start, end = body(reader_src, 'h_srv__recv_line')
part = once(reader_src[start:end], 'if (h_line_len > 8192LL)', 'if (0)')
mutant = build(reader_src[:start] + part + reader_src[end:], 'reader-cap-mutant', links)
p = run([mutant, 'db', OUT / 'db-over-lf.data', '4096'], 'reader-cap-mutant-run')
if p.stdout != 'line=8193\nline=6\n':
    raise RuntimeError('line-cap defeated control did not admit the oversized line')
report['line_cap_defeated_control'] = 'oversized line admitted when pre-split check removed'

# Replay only mandatory concurrent payload storage, NOT an optimized reader.
# Real dual cursors versus ordinary native and presized single-headed allocation.
layout = rt + '\n#include "dual.h"\n' + r'''
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    size_t sizes[3], total = 0;
    for (int i = 0; i < 3; i++) {
        size_t n = (size_t)strtoull(argv[i+1], NULL, 10);
        if (n > 2097152) return 2;
        sizes[i] = n ? (n + 16) & ~(size_t)7 : 0; total += sizes[i];
    }
    size_t budget = (size_t)strtoull(argv[4], NULL, 10);
    for (int mode = 0; mode < 5; mode++) {
        Arena a = arena_new(mode == 1 ? 4096 : mode == 2 ? total : 65536);
        Dual d = {0}; unsigned char *p[3]; int fits = 1;
        if (mode >= 3 && !dual_init(&d, mode == 3 ? budget : total)) return 3;
        unsigned char *whole = mode == 2 && total ? arena_alloc(&a, total) : NULL;
        size_t offset = 0;
        for (int i = 0; i < 3; i++) {
            p[i] = !sizes[i] ? NULL : mode >= 3 ? dual_alloc(&d, sizes[i], i == 0) :
                mode == 2 ? whole + offset : arena_alloc(&a, sizes[i]);
            offset += sizes[i];
            if (sizes[i] && !p[i]) fits = 0;
            if (p[i]) memset(p[i], 17+i, sizes[i]);
        }
        for (int i = 0; i < 3; i++) if (p[i]) for (size_t j = 0; j < sizes[i]; j++)
            if (p[i][j] != 17+i) return 4;
        size_t cap = 0;
        for (HBlock *b = a.head; b; b = b->next) cap += b->cap;
        printf("%d %d %zu %zu\n", mode, fits, mode >= 3 ? d.cap : cap, total);
        if (mode >= 3) dual_free(&d);
        arena_free(&a); tycho_pool_flush();
    }
}
'''
layout_bin = build(layout, 'layout', ['-I', 'bench/allocator'])
for row in report['readers']:
    for point in row['phases']:
        if point[0] not in (1, 21):
            continue
        budget = 1048576 if row['case'].startswith('http') else 8192
        p = run([layout_bin] + list(map(str, point[1:])) + [str(budget)],
                'layout-' + row['case'] + '-' + str(row['chunk_limit']) + '-' + row['block'] + '-' + str(len(report['layout'])))
        values = [list(map(int, line.split())) for line in p.stdout.splitlines()]
        if values[2][2] != values[4][2]:
            raise RuntimeError('exact-budget dual and presized native disagree')
        if values[3][1] != (values[3][3] <= budget):
            raise RuntimeError('dual collision acceptance differs from live size bound')
        report['layout'].append({'case': row['case'], 'groups': point[1:], 'budget': budget, 'rows': values})
if subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip() != fingerprint:
    raise RuntimeError('source tree changed during measurement')
report.update({'runtime_sha256': hashlib.sha256(rt.encode()).hexdigest(),
    'source_fingerprint': fingerprint,
    'artifacts': str(OUT.relative_to(ROOT)),
    'limits': ['Directory timing/RSS use uninstrumented binaries; profile and layout replay are separate.',
               'Small-native changes the whole application default block size; it is not a local caller patch.',
               'Wide snapshot uses controlled filenames and repository-derived content, not ordinary checkout names.',
               'Reader IPC fixture owns a native input copy; arena counters exclude it, RSS would include it.',
               'Layout replay is a lower-bound simultaneous-payload test, not optimized-reader timing.',
               'Presized native and exact-budget dual have identical minimum payload capacity; replay excludes parser temporaries.',
               'No new production integration is authorized by a successful finite replay alone.']})
text = json.dumps(report, indent=2) + '\n'
(OUT / 'results.json').write_text(text)
(OUT / 'publish.json').write_text(text)
os.replace(OUT / 'publish.json', BASE / ('readers.json' if options.readers_only else 'results.json'))
print('Artifacts:', OUT)
