#!/usr/bin/env python3
"""Attribute actual application capacity peaks; compare targeted source changes.

Controls are historical emitted/source copies, never edits to production files.
Timing/RSS runs contain no instrumentation. Peak rows are simultaneous occupied
block offsets, INCLUDING dead/recycled chunks, not live values or RSS. POSIX only.
"""
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

from profile_runtime import instrument_peaks, once

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
BASE = ROOT / 'build/allocator-peaks'
BASE.mkdir(parents=True, exist_ok=True)
OUT = Path(tempfile.mkdtemp(prefix='run-', dir=BASE))
CC = shlex.split(os.environ.get('CC', 'cc'))
FLAGS = ['-O2', '-fwrapv', '-ffp-contract=off']
ENV = dict(os.environ, TYCHO_ARENA_STATS='0', TYCHO_CORELIB=str(ROOT / 'corelib'))
ENV.pop('TYCHO_BLOCK', None)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
COMPILER = os.environ.get('TYCHOC', './tychoc1')
fingerprint = subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip()
report = {'cc': CC, 'flags': FLAGS, 'source_fingerprint': fingerprint, 'cases': [],
          'meaning': 'first maximum active backing capacity; simultaneous owner rows are occupied offsets, not live values',
          'controls': {'presized': 'docs: reserve upper bounds in strip_tags/json_esc only; ZIP: reserve uncompressed upper bound in native int vector',
                       'small-native': 'historical source, whole-application TYCHO_BLOCK=4096'}}


def run(cmd, label, env=ENV, ok=True):
    p = subprocess.run([str(x) for x in cmd], env=env, text=True, capture_output=True, timeout=300)
    (OUT / (label + '.stdout')).write_text(p.stdout)
    (OUT / (label + '.stderr')).write_text(p.stderr)
    if ok and p.returncode:
        raise RuntimeError(label + ': ' + p.stderr[-3000:])
    return p


def body(src, name):
    m = re.search(r'^\w[^\n]*\b' + re.escape(name) + r'\([^\n]*\) \{\n', src, re.M)
    if not m:
        raise RuntimeError('function missing: ' + name)
    return m.start(), src.index('\n}', m.end()) + 2


def build(src, name, links, extra=()):
    path = OUT / (name + '.c')
    path.write_text(src)
    binary = OUT / name
    run(CC + FLAGS + list(extra) + [path] + links + ['-lm', '-pthread', '-o', binary], 'build-' + name)
    return binary


def emit(source, name, env=ENV):
    dest = OUT / name
    run([COMPILER, source, '--emit-c', '-o', dest], 'emit-' + name, env)
    links = run([COMPILER, source, '--print-shims'], 'shims-' + name, env).stdout.splitlines()
    deps = run([COMPILER, source, '--print-deps'], 'deps-' + name, env).stdout.splitlines()
    if deps:
        links += shlex.split(run(['pkg-config', '--cflags', '--libs'] + deps, 'deps-flags-' + name).stdout)
    return dest.with_suffix('.c').read_text(), links


def digest(path):
    if path.is_dir():
        manifest = [(str(f.relative_to(path)), hashlib.sha256(f.read_bytes()).hexdigest())
                    for f in sorted(path.rglob('*')) if f.is_file()]
        return hashlib.sha256(json.dumps(manifest).encode()).hexdigest()
    return hashlib.sha256(path.read_bytes()).hexdigest()


def clear(path):
    if OUT not in path.parents:
        raise RuntimeError('output escapes run directory')
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def parse_profile(stderr):
    m = re.search(r'^peak-point: (\d+) (\d+) (\d+) (.+)$', stderr, re.M)
    g = re.search(r'^allocator-profile: (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)$', stderr, re.M)
    if not m or not g or int(g[6]) or m[1] != g[1]:
        raise RuntimeError('missing/inconsistent peak or surviving ownership')
    rows = [(name, int(n)) for name, n in re.findall(r'^peak-label: (.+)\t(\d+)$', stderr, re.M)]
    if sum(n for _, n in rows) != int(m[2]):
        raise RuntimeError('peak owner rows do not sum')
    return {'peak_capacity_B': int(m[1]), 'bump_at_peak_B': int(m[2]),
            'tail_at_peak_B': int(m[1]) - int(m[2]), 'acquiring_owner': m[4],
            'cumulative_reserved_at_peak_B': int(m[3]),
            'cumulative_reserved_B': int(re.search(r'^peak-final-os: (\d+)$', stderr, re.M)[1]),
            'owners_at_peak': sorted(rows, key=lambda row: -row[1])}


def profiled(src):
    return once(instrument_peaks(src), 'static void peak_dump(void) {',
                'static void peak_dump(void) {\n    fprintf(stderr, "peak-final-os: %zu\\n", (size_t)atomic_load(&st_os_bytes));')


# Calibrate simultaneous owners at a known first high-water, including reset/free.
rt = Path('runtime/tycho_rt.c').read_text()
cal = profiled(rt) + '''
int main(void) {
    Arena a = arena_new(256); a.name = "calibration";
    arena_alloc(&a, 24);
    Arena b = arena_new(128); b.name = "scratch";
    arena_alloc(&b, 16);
    if (peak_cap != 384 || peak_bump != 40) abort();
    arena_reset(&b); arena_free(&b); arena_free(&a); tycho_pool_flush(); return 0;
}
'''
p = run([build(cal, 'calibration', [])], 'calibration', dict(ENV, TYCHO_ARENA_STATS='full'))
assert parse_profile(p.stderr)['owners_at_peak'] == [('calibration', 24), ('scratch', 16)]
bad = once(cal, 'sum += peak_rows[i];', 'sum += 0;')
p = run([build(bad, 'defeated', [])], 'defeated', dict(ENV, TYCHO_ARENA_STATS='full'), ok=False)
if not p.returncode or 'peak-profile: owner sum mismatch' not in p.stderr:
    raise RuntimeError('defeated snapshot accounting not detected')

# Reconstruct only the historical writer; all other library code stays current.
lib = OUT / 'baseline-corelib'
shutil.copytree(ROOT / 'corelib', lib)
s = (lib / 'zip/zip.ty').read_text()
a, b = s.index('fn create_checked('), s.index('# Compatibility wrapper')
historical = Path('bench/allocator/zip_writer_baseline.src').read_text()
old_writer, old_helpers = historical.split('fn _push_str', 1)
s = s[:a] + old_writer + s[b:s.index('fn _push_str')] + 'fn _push_str' + old_helpers
(lib / 'zip/zip.ty').write_text(s)
zip_env = dict(ENV, TYCHO_CORELIB=str(lib))
# Ablation: compact size/CRC metadata with the OLD int-vector builder.
meta_lib = OUT / 'metadata-corelib'
shutil.copytree(ROOT / 'corelib', meta_lib)
meta = (meta_lib / 'zip/zip.ty').read_text()
meta = once(meta, 'out := to_bytes([]int)', 'out := []int')
meta = once(meta, 'return Ok(out)', 'return Ok(to_bytes(out))')
meta = meta[:meta.index('fn _push_str')] + 'fn _push_str' + old_helpers
(meta_lib / 'zip/zip.ty').write_text(meta)
metadata_env = dict(ENV, TYCHO_CORELIB=str(meta_lib))

# Reconstruct all twelve original one-byte append sites. Three search-helper
# sites were the first hotspot; the same pattern then dominated page rendering.
docs = Path('tools/tycho-docs/main.ty').read_text()
docs = once(docs, '        # Per-page intermediates belong to this loop, not the growing index.\n        search_title := json_esc(titles[i])\n        search_text := json_esc(strip_tags(body))\n', '')
docs = once(docs, ' + search_title + ', ' + json_esc(titles[i]) + ')
docs = once(docs, ' + search_text + ', ' + json_esc(strip_tags(body)) + ')
for name, var, count in [('slug', 's', 2), ('split_cells', 's', 1),
                         ('render_spans', 'e', 5), ('published_links', 'content', 1),
                         ('strip_tags', 's', 2), ('json_esc', 's', 1)]:
    start = docs.index('fn ' + name + '(')
    end = docs.index('\nfn ', start + 1)
    part = docs[start:end]
    # Current optimized spelling is deliberate and guarded, not a broad rewrite.
    char = 'char_at(' + var + ', i)'
    if part.count(char) != count:
        raise RuntimeError('historical docs helper anchor drifted: ' + name)
    docs = docs[:start] + part.replace(char, var + '[i:i+1]') + docs[end:]
old_docs = OUT / 'historical-docs/main.ty'
old_docs.parent.mkdir()
old_docs.write_text(docs)

run(CC + ['-O2', 'bench/peakrss.c', '-o', OUT / 'peakrss'], 'build-meter')
wide = OUT / 'wide'
wide.mkdir()
for i in range(700):
    (wide / (f'{i:06}-' + chr(97 + i % 26) * 57)).write_bytes(Path('compiler/main.ty').read_bytes()[:128])
for label, root in [('corelib', ROOT / 'corelib'), ('wide', wide)]:
    (OUT / (label + '.toml')).write_text('[snapshot]\nname = "peak-' + label + '"\nroot = ' + json.dumps(str(root)) + '\n')

for app, source in [('compiler', 'compiler/main.ty'), ('docs', 'tools/tycho-docs/main.ty'), ('snapshot', 'tools/tycho-snap/main.ty')]:
    current, links = emit(source, app + '-current')
    baseline, base_links = (emit(old_docs, 'docs-baseline') if app == 'docs' else
                            emit(source, app + '-baseline', zip_env) if app == 'snapshot' else (current, links))
    presized = baseline
    if app == 'docs':
        # Differential helper probe: every byte, every short prefix (including
        # unterminated tags/NUL), plus an 8 KiB binary input. No UTF-8 assumptions.
        probe = once(current, 'int main(int argc, char **argv) {', 'int ignored_application_main(int argc, char **argv) {')
        for helper in ('strip_tags', 'json_esc', 'slug', 'render_spans'):
            a, b = body(baseline, 'h_' + helper)
            probe += '\n' + baseline[a:b].replace('h_' + helper + '(', 'h_old_' + helper + '(')
        probe += r'''
int main(void) {
    for (int k = 0; k <= 257; k++) {
        int n = k == 257 ? 8192 : k;
        Arena a = arena_new(256);
        char *s = tycho_str_alloc(&a, n);
        for (int i = 0; i < n; i++) s[i] = (char)i;
        char *old[4] = {h_old_strip_tags(&a, s), h_old_json_esc(&a, s), h_old_slug(&a, s), h_old_render_spans(&a, s)};
        char *now[4] = {h_strip_tags(&a, s), h_json_esc(&a, s), h_slug(&a, s), h_render_spans(&a, s)};
        for (int j = 0; j < 4; j++)
            if (tycho_str_len(old[j]) != tycho_str_len(now[j]) ||
                memcmp(old[j], now[j], (size_t)tycho_str_len(old[j]))) {
                fputs("peak-docs: byte helper changed\n", stderr); return 1;
            }
        arena_free(&a);
    }
    tycho_pool_flush(); return 0;
}
'''
        run([build(probe, 'docs-byte-probe', links)], 'docs-byte-probe')
        sanitized = build(probe, 'docs-byte-probe-sanitized', links,
                          ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
        run([sanitized], 'docs-byte-probe-sanitized', dict(ENV, ASAN_OPTIONS='detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1'))
        report['docs_byte_probe'] = 'all 256 bytes/prefixes and 8 KiB binary input agree; native and ASan/UBSan/LSan'

        for name, factor in [('strip_tags', 1), ('json_esc', 2)]:
            a, b = body(presized, 'h_' + name)
            reserve = '''
    tycho_int input = tycho_str_len(h_s);
    if (input > (INT64_MAX - 1) / FACTOR) tycho_oom();
    tycho_str_append_room(_parent, &h_out, &_append_h_out, input * FACTOR);
    ((tycho_int *)h_out)[-1] = 0; h_out[0] = 0;
'''.replace('FACTOR', str(factor))
            part = once(presized[a:b], 'TychoStrAppend _append_h_out = {0};', 'TychoStrAppend _append_h_out = {0};' + reserve)
            presized = presized[:a] + part + presized[b:]
    elif app == 'snapshot':
        a, b = body(presized, 'h_zip__create_checked')
        part = presized[a:b]
        m = re.search(r'    TychoArrInt h_out = [^\n]+;', part)
        if not m:
            raise RuntimeError('historical ZIP vector anchor drifted')
        reserve = '''
    tycho_int upper = 22;
    for (tycho_int j = 0; j < h_entries.len; j++)
        upper += 76 + 2 * tycho_str_len(h_entries.data[j].f_name) + tycho_str_len(h_entries.data[j].f_data);
    /* Validated entry count/field sizes keep upper below 2^49. Fall back for
       huge compressible inputs rather than reject a valid classic archive. */
    if (upper <= 4294967295LL && (uint64_t)upper <= SIZE_MAX / sizeof(tycho_int))
        h_out = tycho_arr_int_with_cap(&_scope, upper);
'''
        part = part[:m.end()] + reserve + part[m.end():]
        presized = presized[:a] + part + presized[b:]
    variants = {}
    modes = [('current', current, links, None)] if app == 'compiler' else [
        ('baseline', baseline, base_links, None), ('presized', presized, base_links, None),
        ('small-native', baseline, base_links, '4096'), ('current', current, links, None)]
    if app == 'docs':
        search_only = baseline
        for name in ('strip_tags', 'json_esc'):
            a, b = body(search_only, 'h_' + name)
            c, d = body(current, 'h_' + name)
            search_only = search_only[:a] + current[c:d] + search_only[b:]
        modes.insert(1, ('search-characters-only', search_only, base_links, None))
        characters = baseline
        for name in ('slug', 'split_cells', 'render_spans', 'published_links', 'strip_tags', 'json_esc'):
            a, b = body(characters, 'h_' + name)
            c, d = body(current, 'h_' + name)
            characters = characters[:a] + current[c:d] + characters[b:]
        modes.insert(2, ('characters-only', characters, base_links, None))
    if app == 'snapshot':
        metadata, metadata_links = emit(source, 'snapshot-metadata', metadata_env)
        modes.insert(1, ('compact-metadata', metadata, metadata_links, None))
    for mode, code, deps, block in modes:
        plain = build(code, app + '-' + mode, deps)
        traced = build(profiled(code), app + '-' + mode + '-profile', deps, ['-g', '-rdynamic'])
        variants[mode] = (plain, traced, dict(ENV, **({'TYCHO_BLOCK': block} if block else {})))
    cases = [(app, ['compiler/main.ty', '--emit-c', '-o', OUT / 'output'], OUT / 'output.c')] if app == 'compiler' else (
        [(app, ['docs', OUT / 'site'], OUT / 'site')] if app == 'docs' else
        [('snapshot-' + label, ['--manifest=' + str(OUT / (label + '.toml')), '--out=' + str(OUT / 'output.zip'), '--quiet'], OUT / 'output.zip') for label in ('corelib', 'wide')])
    for case, args, output in cases:
        expected = None
        rows = []
        for mode, _, _, _ in modes:
            plain, traced, env = variants[mode]
            clear(output)
            p = run([traced] + args, case + '-' + mode + '-profile', dict(env, TYCHO_ARENA_STATS='full'))
            signature = (p.stdout, digest(output))
            if expected is None:
                expected = signature
            if signature != expected:
                raise RuntimeError(case + ': changed output')
            if app == 'snapshot':
                import zipfile
                with zipfile.ZipFile(output) as archive:
                    assert archive.testzip() is None
            row = {'case': case, 'mode': mode, 'output_sha256': signature[1], **parse_profile(p.stderr), 'samples': []}
            rows.append(row)
        for round_ in range(6):
            for row in rows if round_ % 2 == 0 else reversed(rows):
                plain, _, env = variants[row['mode']]
                clear(output)
                p = run([OUT / 'peakrss', plain] + args, case + '-' + row['mode'] + '-' + str(round_), env)
                if (p.stdout, digest(output)) != expected:
                    raise RuntimeError(case + ': plain/profile output differs')
                rss, ms = map(int, p.stderr.splitlines()[-1].split())
                if platform.system() == 'Darwin':
                    rss //= 1024
                if round_:
                    row['samples'].append({'wall_ms': ms, 'rss_KiB': rss})
        for row in rows:
            row['median_wall_ms'] = statistics.median(s['wall_ms'] for s in row['samples'])
            row['median_rss_KiB'] = statistics.median(s['rss_KiB'] for s in row['samples'])
            print(case, row['mode'], row['peak_capacity_B'], row['median_rss_KiB'], row['median_wall_ms'], row['owners_at_peak'][:4], flush=True)
        report['cases'] += rows

end = subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip()
if end != fingerprint:
    raise RuntimeError('source changed during measurements; rerun')
report['artifacts'] = str(OUT)
(OUT / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
(OUT / 'publish.json').write_text(json.dumps(report, indent=2) + '\n')
os.replace(OUT / 'publish.json', BASE / 'results.json')
print('Artifacts:', OUT)
