#!/usr/bin/env python3
"""Small mutation campaign over corelib, the compiler and the runtime.

Each case copies the subject, applies one semantic edit, rebuilds the matching
fixture and requires its golden to move. A surviving mutant is a test gap, not a
compiler bug. The two controls prove the harness can both kill (RF_OK changed)
and stay quiet (comment-only edit).
"""
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, 'tychoc1')

CASES = [
    {
        'name': 'control-kill-rf-ok',
        'file': 'corelib/io/io.ty',
        'old': 'const RF_OK   = 1',
        'new': 'const RF_OK   = 2',
        'entry': 'corelib/test/io/main.ty',
        'golden': 'corelib/test/io.out',
        'control': 'kill',
    },
    {
        'name': 'control-neutral-comment',
        'file': 'corelib/io/io.ty',
        'old': '# All of stdin as raw bytes.',
        'new': '# All of stdin as raw bytes (neutral edit).',
        'entry': 'tests/pkg/stdin_bytes/main.ty',
        'golden': 'tests/pkg/stdin_bytes.out',
        'input': 'tests/pkg/stdin_bytes.in',
        'control': 'survive',
    },
    {
        'name': 'io-read-stdin-wrong-status',
        'file': 'corelib/io/io.ty',
        'old': 'fn read_stdin_bytes() -> Result(bytes, IoErr):\n    st := 0\n    b := iox_read_stdin(&st)\n    if st == RF_OK:',
        'new': 'fn read_stdin_bytes() -> Result(bytes, IoErr):\n    st := 0\n    b := iox_read_stdin(&st)\n    if st == RF_MISS:',
        'entry': 'tests/pkg/stdin_bytes/main.ty',
        'golden': 'tests/pkg/stdin_bytes.out',
        'input': 'tests/pkg/stdin_bytes.in',
    },
    {
        'name': 'io-read-link-missing-ok',
        'file': 'corelib/io/io.ty',
        'old': 'fn read_link(p: string) -> Result(string, IoErr):\n    if _has_nul(p):\n        return Err(BadPath)\n    st := 0\n    b := iox_read_link(p, &st)\n    if st == RF_OK:\n        return Ok(to_str(b))\n    if st == RF_MISS:\n        return Err(NotFound)',
        'new': 'fn read_link(p: string) -> Result(string, IoErr):\n    if _has_nul(p):\n        return Err(BadPath)\n    st := 0\n    b := iox_read_link(p, &st)\n    if st == RF_OK:\n        return Ok(to_str(b))\n    if st == RF_MISS:\n        return Ok("x")',
        'entry': 'corelib/test/io/main.ty',
        'golden': 'corelib/test/io.out',
    },
    {
        'name': 'io-shim-link-length-zero',
        'file': 'corelib/io/io_shim.c',
        'old': '            *outlen = (tycho_int)n;\n            *status = TY_RF_OK;',
        'new': '            *outlen = 0;\n            *status = TY_RF_OK;',
        'entry': 'corelib/test/io/main.ty',
        'golden': 'corelib/test/io.out',
    },
    {
        'name': 'net-write-timeout-failed',
        'file': 'corelib/net/net.ty',
        'old': '    if st == WR_TMO:\n        return Err(Timeout)',
        'new': '    if st == WR_TMO:\n        return Err(Failed)',
        'entry': 'corelib/test/net/main.ty',
        'golden': 'corelib/test/net.out',
    },
    {
        'name': 'net-arm-write-timeout-false',
        'file': 'corelib/net/net.ty',
        'old': 'fn set_write_timeout_ms(fd: int, ms: int) -> bool:\n    return netx_set_write_timeout(fd, ms) == 1',
        'new': 'fn set_write_timeout_ms(fd: int, ms: int) -> bool:\n    return false',
        'entry': 'corelib/test/net/main.ty',
        'golden': 'corelib/test/net.out',
    },
    {
        'name': 'os-capture-drops-stderr',
        'file': 'corelib/os/os.ty',
        'old': '    err := to_str(osx_run_err(r))',
        'new': '    err := ""',
        'entry': 'corelib/test/os/main.ty',
        'golden': 'corelib/test/os.out',
    },
    {
        'name': 'strings-parse-u64-rejects-max',
        'file': 'corelib/strings/strings.ty',
        'old': '        if v > (max - d) / ten:',
        'new': '        if v >= (max - d) / ten:',
        'entry': 'corelib/test/strings/main.ty',
        'golden': 'corelib/test/strings.out',
    },
    {
        'name': 'json-empty-array-trailing',
        'file': 'corelib/json/json.ty',
        'old': '        if pos != n:\n            return Err(TrailingText(pos))\n        return Ok(JArr(xs))',
        'new': '        return Ok(JArr(xs))',
        'entry': 'corelib/test/json/main.ty',
        'golden': 'corelib/test/json.out',
    },
    {
        'name': 'runtime-shl-wide-to-one',
        'kind': 'runtime',
        'file': 'runtime/tycho_rt.c',
        'old': 'static tycho_int tycho_shl_i(tycho_int x, long long n) {\n    if (n < 0) { fprintf(stderr, "tycho: negative shift count\\n"); exit(1); }\n    if (n >= 64) return 0;',
        'new': 'static tycho_int tycho_shl_i(tycho_int x, long long n) {\n    if (n < 0) { fprintf(stderr, "tycho: negative shift count\\n"); exit(1); }\n    if (n >= 64) return 1;',
        'entry': 'tests/shift_edge.ty',
        'golden': 'tests/shift_edge.out',
    },
    {
        'name': 'runtime-idiv-to-mul',
        'kind': 'runtime',
        'file': 'runtime/tycho_rt.c',
        'old': 'static tycho_int tycho_idiv(tycho_int a, tycho_int b) {\n    if (b == 0) { fprintf(stderr, "tycho: division by zero\\n"); exit(1); }\n    if (a == INT64_MIN && b == -1) { fprintf(stderr, "tycho: division overflow\\n"); exit(1); }\n    return a / b;\n}',
        'new': 'static tycho_int tycho_idiv(tycho_int a, tycho_int b) {\n    if (b == 0) { fprintf(stderr, "tycho: division by zero\\n"); exit(1); }\n    if (a == INT64_MIN && b == -1) { fprintf(stderr, "tycho: division overflow\\n"); exit(1); }\n    return a * b;\n}',
        'entry': 'tests/boundary_arith.ty',
        'golden': 'tests/boundary_arith.out',
    },
]


def run_case(case):
    tmp = tempfile.mkdtemp(prefix='mut_')
    try:
        dst = os.path.join(tmp, 'corelib')
        shutil.copytree(os.path.join(REPO, 'corelib'), dst)
        kind = case.get('kind', 'corelib')
        extra = []
        compiler = TYCHOC
        if kind == 'corelib':
            path = os.path.join(tmp, case['file'])
        else:
            path = os.path.join(tmp, 'mut_' + os.path.basename(case['file']))
            shutil.copy2(os.path.join(REPO, case['file']), path)
        with open(path) as f:
            text = f.read()
        if text.count(case['old']) != 1:
            return case['name'], 'BROKEN', 'pattern matched %d times' % text.count(case['old'])
        with open(path, 'w') as f:
            f.write(text.replace(case['old'], case['new'], 1))
        if kind == 'compiler':
            compiler = os.path.join(tmp, 'tychoc_mut')
            r = subprocess.run(['cc', '-O2', '-fwrapv', '-std=c11',
                                '-I', os.path.join(REPO, 'build'),
                                path, '-o', compiler], capture_output=True,
                               text=True, timeout=120)
            if r.returncode != 0:
                return case['name'], 'killed', 'mutant compiler failed to build'
        elif kind == 'runtime':
            extra = ['--runtime', path]
        entry = case['entry']
        if entry.startswith('corelib/'):
            entry = os.path.join(dst, entry[len('corelib/'):])
        else:
            entry = os.path.join(REPO, entry)
        golden = os.path.join(REPO, case['golden'])
        binary = os.path.join(tmp, 'test')
        env = dict(os.environ, TYCHO_CORELIB=dst)
        r = subprocess.run([compiler] + extra + [entry, '-o', binary], capture_output=True,
                           text=True, timeout=120, env=env)
        if r.returncode != 0:
            return case['name'], 'killed', 'compile failed: ' + (r.stderr or r.stdout).strip().splitlines()[-1][:120]
        stdin = os.path.join(REPO, case['input']) if case.get('input') else None
        try:
            with open(stdin, 'rb') if stdin else open(os.devnull, 'rb') as inp:
                r = subprocess.run([binary], stdin=inp, capture_output=True, timeout=15,
                                   env=env)
        except subprocess.TimeoutExpired:
            return case['name'], 'killed', 'timeout'
        with open(golden, 'rb') as f:
            want = f.read()
        if r.returncode != 0:
            return case['name'], 'killed', 'run rc=%d' % r.returncode
        if r.stdout != want:
            return case['name'], 'killed', 'golden moved'
        return case['name'], 'survived', None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if not os.access(TYCHOC, os.X_OK):
        print('mutation: missing ./tychoc1; run make')
        return 2
    survivors = 0
    controls = 0
    for case in CASES:
        name, verdict, detail = run_case(case)
        if verdict == 'BROKEN':
            print('FAIL %s (%s)' % (name, detail))
            controls += 1
            continue
        if case.get('control'):
            want = 'killed' if case['control'] == 'kill' else 'survived'
            if verdict != want:
                print('FAIL control %s expected %s, got %s (%s)' % (name, want, verdict, detail))
                controls += 1
            else:
                print('ok   control  %s (%s)' % (name, verdict))
            continue
        if verdict == 'survived':
            print('!!   SURVIVED %s' % name)
            survivors += 1
        else:
            print('ok   killed   %s (%s)' % (name, detail or ''))
    if controls:
        print('mutation: control failure(s)=%d' % controls)
        return 1
    print('mutation: %d case(s), %d survivor(s)' % (len(CASES), survivors))
    return 1 if survivors else 0


if __name__ == '__main__':
    sys.exit(main())
