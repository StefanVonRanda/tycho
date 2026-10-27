#!/usr/bin/env python3
"""Run the concurrency shapes from fuzz/gen.py under ThreadSanitizer.

fuzz/run.py checks generated programs with native -O2 and ASan; ASan does not
see data races. This lane compiles the same seeds with -fsanitize=thread, runs
them, and requires the native and TSan outputs to agree. Seeds without spawn,
channel or parallel-for are skipped so the budget goes to the code that can race.
"""
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, 'fuzz', 'gen.py')
TYCHOC = os.environ.get('TYCHOC') or os.path.join(REPO, 'tychoc1')
FINDINGS = os.path.join(REPO, 'fuzz', 'findings')
CONC = re.compile(r'\b(spawn |channel\(|parallel for )')
TIMEOUT = 60


def build(src, exe, flags):
    r = subprocess.run(['cc'] + flags + [src, os.path.join(REPO, 'fuzz', 'ffi_shim.c'),
                                         '-o', exe], capture_output=True,
                       text=True, timeout=TIMEOUT)
    return r.returncode == 0, r.stderr[:300]


def run(exe, env):
    r = subprocess.run([exe], capture_output=True, text=True, timeout=120, env=env)
    return r.returncode, r.stdout, r.stderr


def check(seed):
    tmp = tempfile.mkdtemp(prefix='tsan_')
    src_text = None
    try:
        g = subprocess.run([sys.executable, GEN, str(seed)], capture_output=True,
                           text=True, timeout=TIMEOUT)
        if g.returncode != 0 or not g.stdout.strip():
            return seed, 'skip', 'generator failed', None
        if not CONC.search(g.stdout):
            return seed, 'skip', None, None
        src_text = g.stdout
        src = os.path.join(tmp, 'p.ty')
        with open(src, 'w') as f:
            f.write(src_text)
        r = subprocess.run([TYCHOC, src, '--emit-c', '-o', os.path.join(tmp, 'p')],
                           capture_output=True, text=True, timeout=TIMEOUT)
        if r.returncode != 0:
            return seed, 'skip', 'compiler rejected generated program', None
        c = os.path.join(tmp, 'p.c')
        native = os.path.join(tmp, 'native')
        tsan = os.path.join(tmp, 'tsan')
        ok, err = build(c, native, ['-O2', '-fwrapv', '-std=c11', '-pthread'])
        if not ok:
            return seed, 'FAIL', 'native cc: ' + err, src_text
        ok, err = build(c, tsan, ['-O1', '-fwrapv', '-std=c11', '-pthread',
                                  '-fsanitize=thread', '-g'])
        if not ok:
            return seed, 'FAIL', 'tsan cc: ' + err, src_text
        env = dict(os.environ, TSAN_OPTIONS='halt_on_error=1:exitcode=66')
        try:
            nr, nout, nerr = run(native, env)
            tr, tout, terr = run(tsan, env)
        except subprocess.TimeoutExpired:
            return seed, 'FAIL', 'timeout', src_text
        if nr != 0:
            return seed, 'FAIL', 'native rc=%d %s' % (nr, nerr[:200]), src_text
        if tr != 0 or 'ThreadSanitizer' in terr:
            return seed, 'FAIL', 'tsan rc=%d %s' % (tr, terr[:300]), src_text
        if nout != tout:
            return seed, 'FAIL', 'native/tsan output diverge', src_text
        return seed, 'ok', None, None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 64
    if n <= 0:
        print('fuzz-tsan: skipped (N=0)')
        return 0
    if not os.access(TYCHOC, os.X_OK):
        print('missing %s; run make' % TYCHOC)
        return 2
    if subprocess.run(['cc', '-fsanitize=thread', '-x', 'c', '-', '-o', '/dev/null'],
                      input='int main(void){return 0;}\n', text=True,
                      capture_output=True).returncode != 0:
        print('fuzz-tsan: skip (cc has no ThreadSanitizer runtime)')
        return 0
    os.makedirs(FINDINGS, exist_ok=True)
    jobs = int(os.environ.get('FUZZ_JOBS', 0)) or max(1, (os.cpu_count() or 4) - 2)
    ran = skipped = fails = 0
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(check, seed) for seed in range(1, n + 1)]
        for fut in concurrent.futures.as_completed(futures):
            seed, verdict, detail, src = fut.result()
            if verdict == 'ok':
                ran += 1
            elif verdict == 'skip':
                skipped += 1
            else:
                print('FAIL seed=%d %s' % (seed, detail))
                if src is not None:
                    os.makedirs(FINDINGS, exist_ok=True)
                    with open(os.path.join(FINDINGS, 'tsan_seed_%d.ty' % seed), 'w') as f:
                        f.write(src)
                fails += 1
    print('fuzz-tsan: %d ran, %d skipped, %d fail(s)' % (ran, skipped, fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
