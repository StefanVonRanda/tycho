#!/usr/bin/env python3
"""Compare compiler caller changes in isolated source/emitted-C copies.

Run before integration, or reconstruct historical source using --before TREE.
No production source modification. Plain timing/RSS are separate from profiles.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shlex
import statistics
import subprocess
import tempfile

from profile_runtime import instrument_peaks, once

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--before', help='Git tree containing the pre-integration compiler sources')
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
BASE = ROOT / 'build/compiler-peak'
BASE.mkdir(parents=True, exist_ok=True)
OUT = Path(tempfile.mkdtemp(prefix='run-', dir=BASE))
CC = shlex.split(os.environ.get('CC', 'cc'))
FLAGS = ['-O2', '-fwrapv', '-ffp-contract=off']
ENV = dict(os.environ, TYCHO_CORELIB=str(ROOT / 'corelib'), TYCHO_ARENA_STATS='0')
ENV.pop('TYCHO_BLOCK', None)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
HOST = os.environ.get('TYCHOC', './tychoc1')
fingerprint = subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip()
report = {'cc': CC, 'cc_version': subprocess.check_output(CC + ['--version'], text=True).splitlines()[0],
          'flags': FLAGS, 'source_fingerprint': fingerprint, 'before_tree': args.before,
          'rows': [], 'sites': ['loaded Program', 'driver AST unit adapter', 'emitter collect1',
                               'emitter collect2', 'statement prefix construction', 'function text accumulation']}


def run(cmd, name, env=ENV, ok=True):
    p = subprocess.run([str(x) for x in cmd], env=env, text=True, capture_output=True, timeout=300)
    (OUT / (name + '.stdout')).write_text(p.stdout)
    (OUT / (name + '.stderr')).write_text(p.stderr)
    if ok and p.returncode:
        raise RuntimeError(name + ': ' + p.stderr[-3000:])
    return p


def build(code, name, links, flags=FLAGS):
    source = OUT / (name + '.c')
    source.write_text(code)
    binary = OUT / name
    run(CC + flags + [source] + links + ['-lm', '-pthread', '-o', binary], 'build-' + name)
    return binary


def body(src, name):
    m = re.search(r'^\w[^\n]*\b' + name + r'\([^\n]*\) \{\n', src, re.M)
    if not m:
        raise RuntimeError('function missing: ' + name)
    return m.start(), src.index('\n}', m.end()) + 2


def replace_function(src, name, transform):
    a, b = body(src, name)
    return src[:a] + transform(src[a:b]) + src[b:]


def profile(src):
    src = instrument_peaks(src)
    helper = r'''
static size_t site_total[6], site_max_owner[6], site_calls[6];
static void site_note(int id, Arena *a, size_t before) {
    size_t after = st_chain_off(a->head);
    if (after < before) { fputs("site-profile: unexpected owner rewind\n", stderr); abort(); }
    site_total[id] += after - before;
    if (after > site_max_owner[id]) site_max_owner[id] = after;
    site_calls[id]++;
}
'''
    src = once(src, 'static void peak_dump(void) {', helper + '\nstatic void peak_dump(void) {\n'
               '    for (int i=0;i<6;i++) fprintf(stderr,"allocation-site: %d %zu %zu %zu\\n",i,site_total[i],site_max_owner[i],site_calls[i]);')
    specs = [('h_driver__compile', 0, lambda s: 'h_driver__check_into(' in s),
             ('h_driver__emit_to', 1, lambda s: 'tycho_copy_S_ast__Unit(' in s and 'f_unit' in s),
             ('h_emit__program_in', 2, lambda s: 'h_emit___collect1(' in s),
             ('h_emit__program_in', 3, lambda s: 'h_emit___collect2(' in s),
             ('h_emit__program_in', 5, lambda s: 'tycho_str_append(' in s and '&h_fns,' in s)]
    serial = 0
    for function, tag, predicate in specs:
        def annotate(part):
            nonlocal serial
            lines = []
            for line in part.splitlines():
                if predicate(line):
                    indent = line[:len(line) - len(line.lstrip())]
                    key = '_site_before_' + str(serial)
                    lines += [indent + 'size_t ' + key + ' = st_chain_off(_scope.head);', line,
                              indent + 'site_note(' + str(tag) + ', &_scope, ' + key + ');']
                    serial += 1
                else:
                    lines.append(line)
            return '\n'.join(lines)
        src = replace_function(src, function, annotate)
    def append_branch(part):
        marker = '                                char *h_state9 ='
        if part.count(marker) != 1:
            raise RuntimeError('append branch marker drifted')
        a = part.index(marker)
        returns = list(re.finditer(r'^.*char \*_ret = (?:tycho_str_copy\(_parent, h_(?:out9|append_code)\)|h_arrays__join__string\(_parent, h_code_parts,.*).*return _ret;.*$', part[a:], re.M))
        if len(returns) != 2:
            raise RuntimeError('append branch returns drifted')
        end = a + returns[-1].end()
        block = part[a:end]
        block = re.sub(r'(^.*char \*_ret = .*return _ret;.*$)',
                       r'                                site_note(4, &_scope, _site_append_start);\n\1', block, flags=re.M)
        return part[:a] + '                                size_t _site_append_start = st_chain_off(_scope.head);\n' + block + part[end:]
    src = replace_function(src, 'h_emit___stmt', append_branch)
    return src


source = {}
for path in sorted(Path('compiler').rglob('*.ty')):
    source[str(path.relative_to('compiler'))] = (subprocess.check_output(['git', 'show', args.before + ':' + str(path)], text=True)
                                                 if args.before else path.read_text())
old_emit, old_driver = source['emit/emit.ty'], source['driver/driver.ty']
# Default runs also work after integration: reconstruct only the two historical
# callers, keeping every other current compiler source unchanged.
if 'fn program_in(program: types.Program,' in old_emit:
    old_emit = once(old_emit, 'import "../types"\n', '')
    old_emit = once(old_emit, "   # distinct from the declaration formatter's out9: keep accumulator recognition", '')
    start = old_emit.index('fn program_in(')
    part = old_emit[start:]
    first = part.index('    for i := 0; i < len(program.files);')
    part = ('fn program_in(pfx: [string], us: [ast.Unit], dbgf: string, fpath: [string], fsrc: [string], fcore: [bool]) -> string:\n'
            '    cs := _new()\n    cs.dbgfile = dbgf\n    cs.fcore = fcore\n'
            '    for fp in fpath:\n        push(cs.fpath, fp)\n'
            '    for fsc in fsrc:\n        push(cs.fsrc, fsc)\n'
            '    for x in pfx:\n        push(cs.fpfx, x)\n' + part[first:])
    part = part.replace('program.files[i].prefix', 'pfx[i]').replace('program.files[i].unit', 'us[i]')
    part = part.replace('len(program.files)', 'len(us)').replace('for file0 in program.files:', 'for u0 in us:')
    old_emit = (old_emit[:start] + part.replace('file0.unit.', 'u0.')).replace('append_code', 'out9')
    adapter = '''    pfx := []string
    us := []ast.Unit
    fpath := []string
    fsrc := []string
    fcore := []bool
    for f in p.files:
        push(pfx, f.prefix)
        push(us, f.unit)
        push(fpath, f.path)
        push(fsrc, f.src)
        push(fcore, types.from_corelib(p.root, f.dir))
    prog := emit.program_in(pfx, us, dbgf, fpath, fsrc, fcore)'''
    old_driver = once(old_driver, '    prog := emit.program_in(p, dbgf)', adapter)
    source['emit/emit.ty'], source['driver/driver.ty'] = old_emit, old_driver
# The measured prefix builder shares the name out9 with a non-accumulator
# declaration-formatting branch. The conservative name-based scan disables it.
a = old_emit.index('                                out9 := ""')
b = old_emit.index('                    _: pass', a)
chunk = old_emit[a:b]
if chunk.count('out9') != 9:
    raise RuntimeError('prefix builder drifted: ' + str(chunk.count('out9')))
append_emit = old_emit[:a] + chunk.replace('out9', 'append_code') + old_emit[b:]
join_chunk = chunk.replace('out9 := ""', 'code_parts := []string')
join_chunk = join_chunk.replace('out9 = out9 + ind + cty8 + " " + nm8 + " = " + tc8 + ";\\n"',
                                'push(code_parts, ind + cty8 + " " + nm8 + " = " + tc8 + ";\\n")')
join_chunk = join_chunk.replace('out9 = out9 + ind + a8', 'push(code_parts, ind + a8)')
join_chunk = join_chunk.replace('out9 = out9 + ind + fnq + "(" + ow9 +\n                                        ", " + ptr9 + ", " + state9 + ", " + tc9 + ");\\n"',
                                'push(code_parts, ind + fnq + "(" + ow9 +\n                                        ", " + ptr9 + ", " + state9 + ", " + tc9 + ");\\n")')
join_chunk = join_chunk.replace('return out9', 'return arrays.join(code_parts, "")')
if 'out9' in join_chunk:
    raise RuntimeError('join transformation drifted')
join_emit = once(old_emit[:a] + join_chunk + old_emit[b:], 'import "core:strings"',
                 'import "core:strings"\nimport "core:arrays"')

# Reuse the existing loaded-program type, not a parallel deep-copied AST array.
def direct(emit, driver):
    emit = once(emit, 'import "../parse"', 'import "../parse"\nimport "../types"')
    start = emit.index('fn program_in(')
    part = emit[start:]
    begin = part.index('    for i := 0; i < len(us);')
    part = ('fn program_in(program: types.Program, dbgf: string) -> string:\n'
            '    cs := _new()\n    cs.dbgfile = dbgf\n'
            '    for file in program.files:\n'
            '        push(cs.fpath, file.path)\n        push(cs.fsrc, file.src)\n'
            '        push(cs.fpfx, file.prefix)\n'
            '        push(cs.fcore, types.from_corelib(program.root, file.dir))\n' + part[begin:])
    part = part.replace('len(us)', 'len(program.files)').replace('us[i]', 'program.files[i].unit')
    part = part.replace('pfx[i]', 'program.files[i].prefix').replace('for u0 in us:', 'for file0 in program.files:')
    part = part.replace('u0.', 'file0.unit.')
    emit = emit[:start] + part
    start, end = driver.index('    pfx := []string'), driver.index('\n    if len(out_c) == 0:')
    driver = driver[:start] + '    prog := emit.program_in(p, dbgf)' + driver[end:]
    return emit, driver

presized_driver = once(old_driver, '    fcore := []bool', '    fcore := []bool\n' +
                       ''.join('    reserve(' + v + ', len(p.files))\n' for v in ['pfx', 'us', 'fpath', 'fsrc', 'fcore']).rstrip())
direct_emit, direct_driver = direct(old_emit, old_driver)
combined_emit, _ = direct(join_emit, old_driver)
append_direct_emit, _ = direct(append_emit, old_driver)
variants = [('baseline', old_emit, old_driver, None), ('presized', old_emit, presized_driver, None),
            ('small-native', old_emit, old_driver, '4096'), ('append-name', append_emit, old_driver, None),
            ('native-join', join_emit, old_driver, None), ('direct-program', direct_emit, direct_driver, None),
            ('combined', combined_emit, direct_driver, None),
            ('append-direct', append_direct_emit, direct_driver, None)]
run(CC + ['-O2', 'bench/peakrss.c', '-o', OUT / 'peakrss'], 'build-meter')
expected = {}
inputs = []
binaries = {}
for mode, emit, driver, block in variants:
    root = OUT / ('source-' + mode)
    for rel, text in source.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(emit if rel == 'emit/emit.ty' else driver if rel == 'driver/driver.ty' else text)
    if mode == 'baseline':
        inputs = [root / 'main.ty', ROOT / 'tools/tycho-docs/main.ty', ROOT / 'tests/inout_string.ty',
                  ROOT / 'tests/value_semantics.ty', ROOT / 'tests/result_void.ty', ROOT / 'tests/conc/implicit.ty']
    dest = OUT / ('compiler-' + mode)
    run([HOST, root / 'main.ty', '--emit-c', '-o', dest], 'emit-' + mode)
    links = run([HOST, root / 'main.ty', '--print-shims'], 'shims-' + mode).stdout.splitlines()
    code = dest.with_suffix('.c').read_text()
    binary = build(code, mode, links)
    instrumented = profile(code)
    traced = build(instrumented, mode + '-profile', links, FLAGS + ['-g', '-rdynamic'])
    if mode == 'baseline':
        cal = once(instrumented, '\nint main(int argc, char **argv) {', '\nint unused_compiler_main(int argc, char **argv) {') + r'''
int main(void) {
    Arena a = arena_new(256); arena_alloc(&a, 24);
    size_t before = st_chain_off(a.head); arena_alloc(&a, 16);
    site_note(0, &a, before);
    if (site_total[0] != 16 || site_max_owner[0] != 40 || site_calls[0] != 1) {
        fputs("site-calibration: wrong delta\n", stderr); return 1;
    }
    arena_free(&a); tycho_pool_flush(); return 0;
}
'''
        run([build(cal, 'site-calibration', links, ['-O0', '-fwrapv'])], 'site-calibration', dict(ENV, TYCHO_ARENA_STATS='full'))
        bad = once(cal, 'site_total[id] += after - before;', 'site_total[id] += 0;')
        defeated = run([build(bad, 'site-defeated', links, ['-O0', '-fwrapv'])], 'site-defeated', dict(ENV, TYCHO_ARENA_STATS='full'), ok=False)
        if not defeated.returncode or 'site-calibration: wrong delta' not in defeated.stderr:
            raise RuntimeError('defeated allocation-site accounting not caught')
    env = dict(ENV, **({'TYCHO_BLOCK': block} if block else {}))
    for n, input_ in enumerate(inputs):
        p = run([binary, input_, '--emit-c', '-o', OUT / 'output'], mode + '-identity-' + str(n), env)
        signature = (p.stdout, p.stderr, hashlib.sha256((OUT / 'output.c').read_bytes()).hexdigest())
        if mode == 'baseline':
            expected[n] = signature
        if signature != expected[n]:
            raise RuntimeError(mode + ': emitted C/diagnostics differ for ' + str(input_))
    p = run([traced, inputs[0], '--emit-c', '-o', OUT / 'output'], mode + '-profile-run', dict(env, TYCHO_ARENA_STATS='full'))
    if hashlib.sha256((OUT / 'output.c').read_bytes()).hexdigest() != expected[0][2]:
        raise RuntimeError('instrumentation changed C')
    match = re.search(r'^peak-point: (\d+) (\d+) (\d+) (.+)$', p.stderr, re.M)
    if not match or not re.search(r'^allocator-profile: .+ 0$', p.stderr, re.M):
        raise RuntimeError('missing peak or surviving arena ownership')
    labels = [(name, int(size)) for name, size in re.findall(r'^peak-label: (.+)\t(\d+)$', p.stderr, re.M)]
    if sum(size for _, size in labels) != int(match[2]):
        raise RuntimeError('peak row sum mismatch')
    binaries[mode] = (binary, env)
    sites = [list(map(int, values)) for values in re.findall(r'^allocation-site: (\d+) (\d+) (\d+) (\d+)$', p.stderr, re.M)]
    if len(sites) != 6 or any(not site[3] for site in sites if site[0] != 1):
        raise RuntimeError('allocation-site coverage drifted')
    direct_mode = mode in ('direct-program', 'combined', 'append-direct')
    if bool(sites[1][3]) == direct_mode:
        raise RuntimeError('unit-adapter control drifted')
    row = {'mode': mode, 'compiler_c_sha256': hashlib.sha256(code.encode()).hexdigest(),
           'peak_capacity_B': int(match[1]), 'occupied_at_peak_B': int(match[2]),
           'owners': sorted(labels, key=lambda x: -x[1]), 'samples': [],
           'sites': sites,
           'site_fields': ['id', 'total_owner_offset_increment_B', 'max_owner_offset_after_site_B', 'calls']}
    report['rows'].append(row)
    print(mode, row['peak_capacity_B'], row['owners'][:4], flush=True)
# Warmup plus five interleaved plain rounds, reversing order each round.
for round_ in range(6):
    for row in report['rows'] if round_ % 2 == 0 else reversed(report['rows']):
        mode = row['mode']
        binary, env = binaries[mode]
        p = run([OUT / 'peakrss', binary, inputs[0], '--emit-c', '-o', OUT / 'output'], mode + '-plain-' + str(round_), env)
        if hashlib.sha256((OUT / 'output.c').read_bytes()).hexdigest() != expected[0][2]:
            raise RuntimeError('plain output changed')
        rss, ms = map(int, p.stderr.splitlines()[-1].split())
        if round_:
            row['samples'].append({'rss_KiB': rss, 'wall_ms': ms})
for row in report['rows']:
    row['median_rss_KiB'] = statistics.median(s['rss_KiB'] for s in row['samples'])
    row['median_wall_ms'] = statistics.median(s['wall_ms'] for s in row['samples'])
    print(row['mode'], row['peak_capacity_B'], row['median_rss_KiB'], row['median_wall_ms'], flush=True)
if subprocess.check_output(['sh', 'scripts/status.sh', '--fingerprint'], text=True).strip() != fingerprint:
    raise RuntimeError('source drift during measurement')
report['artifacts'] = str(OUT)
report['output_sha256'] = expected[0][2]
report['inputs'] = [{'path': str(path), 'c_sha256': expected[n][2], 'stdout': expected[n][0],
                     'stderr': expected[n][1]} for n, path in enumerate(inputs)]
report['site_calibration_and_defeated_control'] = True
report['limits'] = ['Offsets include dead/recycled chunks, not live values.',
                    'Site deltas measure owner occupied-offset growth, not semantic copy bytes.',
                    'Interleaved distributions are not universal speedup or latency guarantees.',
                    'Frozen pre-integration compiler tree is the common heavy input.']
(OUT / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
(OUT / 'publish.json').write_text(json.dumps(report, indent=2) + '\n')
os.replace(OUT / 'publish.json', BASE / 'results.json')
print('Artifacts:', OUT)
