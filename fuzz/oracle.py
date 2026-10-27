#!/usr/bin/env python3
"""Generated integer programs checked against an independent Python oracle.

The existing fuzzers compare the compiler against itself (native vs ASan, or
both compilers). This one evaluates the same AST in Python with Tycho's
64-bit wrap/truncating-division rules, so a compiler that is consistently wrong
still fails. It targets int arithmetic, comparisons, if/else, for loops and
function calls; it does not replace the value-semantics fuzzer.
"""
import argparse
import concurrent.futures
import os
import random
import subprocess
import sys
import tempfile

MASK = (1 << 64) - 1
INT64_MIN = -(1 << 63)

LITERALS = [0, 1, -1, 2, 3, -2, 7, -7, 13, -13, 255, 256, 65535, 65536,
            4294967296, -4294967296, 4611686018427387904, -4611686018427387904,
            9223372036854775807, -9223372036854775807]
DIVISORS = [1, 2, 3, 5, 7, -2, -3, -5, -7]
SHIFTS = [0, 1, 7, 31, 32, 62, 63, 64, 65]
ARGS = [(0, 1, -1), (7, -13, 42), (4611686018427387904, 3, -7),
        (9223372036854775807, 9223372036854775807, -9223372036854775807),
        (-9223372036854775807, 256, 65535)]


def wrap(v):
    v &= MASK
    return v - (1 << 64) if v >= (1 << 63) else v


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def c_rem(a, b):
    return a - trunc_div(a, b) * b


def shift_right(a, n):
    return 0 if n >= 64 else a >> n


class Generator:
    def __init__(self, seed):
        self.r = random.Random(seed)

    def leaf(self, env):
        if env and self.r.random() < 0.65:
            return ('var', self.r.choice(sorted(env)))
        return ('lit', self.r.choice(LITERALS))

    def expr(self, env, depth):
        if depth <= 0 or self.r.random() < 0.30:
            return self.leaf(env)
        if self.r.random() < 0.12:
            return ('un', self.r.choice(['-', '~']), self.expr(env, depth - 1))
        op = self.r.choice(['+', '-', '*', '&', '|', '^', '<<', '>>', '/', '%'])
        if op in ('/', '%'):
            return ('div', op, self.expr(env, depth - 1), self.r.choice(DIVISORS))
        if op in ('<<', '>>'):
            return ('shift', op, self.expr(env, depth - 1), self.r.choice(SHIFTS))
        return ('bin', op, self.expr(env, depth - 1), self.expr(env, depth - 1))

    def cond(self, env, depth):
        if depth > 0 and self.r.random() < 0.20:
            return ('not', self.cond(env, depth - 1))
        if depth > 0 and self.r.random() < 0.40:
            return ('log', self.r.choice(['and', 'or']),
                    self.cond(env, depth - 1), self.cond(env, depth - 1))
        return ('cmp', self.r.choice(['<', '<=', '>', '>=', '==', '!=']),
                self.expr(env, 1), self.expr(env, 1))

    def program(self, nfuncs=12):
        funcs = []
        for _ in range(nfuncs):
            env = {'a', 'b', 'c'}
            first = self.expr(env, 2)
            env.add('x')
            second = self.expr(env, 2)
            env.add('y')
            third = self.expr(env, 2)
            env.add('z')
            funcs.append((first, second, third, self.cond(env, 1),
                          self.expr(env, 2), self.expr(env, 2),
                          self.expr(env | {'i'}, 2), self.expr(env, 3)))
        return funcs


def emit_expr(e):
    kind = e[0]
    if kind == 'lit':
        return str(e[1])
    if kind == 'var':
        return e[1]
    if kind == 'un':
        return '(' + e[1] + '(' + emit_expr(e[2]) + '))'
    if kind == 'bin':
        return '(' + emit_expr(e[2]) + ' ' + e[1] + ' ' + emit_expr(e[3]) + ')'
    if kind in ('div', 'shift'):
        return '(' + emit_expr(e[2]) + ' ' + e[1] + ' ' + str(e[3]) + ')'
    if kind == 'cmp':
        return '(' + emit_expr(e[2]) + ' ' + e[1] + ' ' + emit_expr(e[3]) + ')'
    if kind == 'not':
        return '(not ' + emit_expr(e[1]) + ')'
    if kind == 'log':
        return '(' + emit_expr(e[2]) + ' ' + e[1] + ' ' + emit_expr(e[3]) + ')'
    raise AssertionError(kind)


def evaluate(e, env):
    kind = e[0]
    if kind == 'lit':
        return wrap(e[1])
    if kind == 'var':
        return env[e[1]]
    if kind == 'un':
        v = evaluate(e[2], env)
        return wrap(-v if e[1] == '-' else ~v)
    if kind == 'bin':
        a, b = evaluate(e[2], env), evaluate(e[3], env)
        op = e[1]
        if op == '+': return wrap(a + b)
        if op == '-': return wrap(a - b)
        if op == '*': return wrap(a * b)
        if op == '&': return wrap(a & b)
        if op == '|': return wrap(a | b)
        if op == '^': return wrap(a ^ b)
        raise AssertionError(op)
    if kind == 'div':
        return wrap(trunc_div(evaluate(e[2], env), e[3])
                    if e[1] == '/' else c_rem(evaluate(e[2], env), e[3]))
    if kind == 'shift':
        a = evaluate(e[2], env)
        return wrap(a << e[3]) if e[1] == '<<' else shift_right(a, e[3])
    if kind == 'cmp':
        a, b = evaluate(e[2], env), evaluate(e[3], env)
        return {'<': a < b, '<=': a <= b, '>': a > b, '>=': a >= b,
                '==': a == b, '!=': a != b}[e[1]]
    if kind == 'not':
        return not evaluate(e[1], env)
    if kind == 'log':
        left = evaluate(e[2], env)
        return (left and evaluate(e[3], env)) if e[1] == 'and' \
            else (left or evaluate(e[3], env))
    raise AssertionError(kind)


def run_func(f, a, b, c):
    env = {'a': a, 'b': b, 'c': c}
    env['x'] = evaluate(f[0], env)
    env['y'] = evaluate(f[1], env)
    env['z'] = evaluate(f[2], env)
    if evaluate(f[3], env):
        env['x'] = evaluate(f[4], env)
    else:
        env['y'] = evaluate(f[5], env)
    for i in range(3):
        env['i'] = i
        env['z'] = evaluate(f[6], env)
    del env['i']
    return evaluate(f[7], env)


def emit_program(funcs):
    out = []
    for n, f in enumerate(funcs):
        out.extend([
            'fn f%d(a: int, b: int, c: int) -> int:' % n,
            '    x := ' + emit_expr(f[0]),
            '    y := ' + emit_expr(f[1]),
            '    z := ' + emit_expr(f[2]),
            '    if ' + emit_expr(f[3]) + ':',
            '        x = ' + emit_expr(f[4]),
            '    else:',
            '        y = ' + emit_expr(f[5]),
            '    for i := 0; i < 3; i += 1:',
            '        z = ' + emit_expr(f[6]),
            '    return ' + emit_expr(f[7]),
            '',
        ])
    out.append('fn main():')
    for n, f in enumerate(funcs):
        parts = ['str(f%d(%d, %d, %d))' % (n, a, b, c) for a, b, c in ARGS]
        out.append('    println(' + ' + " " + '.join(parts) + ')')
    return '\n'.join(out) + '\n'


def expected(funcs):
    return '\n'.join(' '.join(str(run_func(f, *args)) for args in ARGS)
                     for f in funcs) + '\n'


def compile_run(src, base, compiler):
    r = subprocess.run([compiler, src, '--emit-c', '-o', base],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return None, '%s rc=%d: %s' % (os.path.basename(compiler), r.returncode,
                                       (r.stderr or r.stdout)[:300])
    r = subprocess.run(['cc', '-O2', '-fwrapv', '-std=c11', '-o', base + '.bin',
                        base + '.c'], capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return None, 'cc rc=%d: %s' % (r.returncode, r.stderr[:300])
    r = subprocess.run([base + '.bin'], capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return None, 'run rc=%d: %s' % (r.returncode, (r.stderr or '')[:300])
    return r.stdout, None


def check_seed(seed, repo, keep):
    funcs = Generator(seed).program()
    want = expected(funcs)
    src_text = emit_program(funcs)
    with tempfile.TemporaryDirectory(prefix='oracle_') as tmp:
        src = os.path.join(tmp, 'p.ty')
        with open(src, 'w') as f:
            f.write(src_text)
        for compiler in (os.path.join(repo, 'tychoc1'), os.path.join(repo, 'tychoc')):
            got, err = compile_run(src, os.path.join(tmp, os.path.basename(compiler)), compiler)
            if err or got != want:
                detail = err or 'output mismatch'
                if got is not None and not err:
                    for i, (w, g) in enumerate(zip(want.splitlines(), got.splitlines())):
                        if w != g:
                            detail = 'line %d want=%r got=%r' % (i + 1, w[:120], g[:120])
                            break
                if keep:
                    os.makedirs(keep, exist_ok=True)
                    with open(os.path.join(keep, 'seed_%d.ty' % seed), 'w') as f:
                        f.write(src_text)
                return seed, os.path.basename(compiler), detail
    return seed, None, None


def selfcheck(repo):
    funcs = Generator(1).program()
    want = expected(funcs).splitlines()
    src_text = emit_program(funcs)
    with tempfile.TemporaryDirectory(prefix='oracle_selfcheck_') as tmp:
        src = os.path.join(tmp, 'p.ty')
        with open(src, 'w') as f:
            f.write(src_text)
        got, err = compile_run(src, os.path.join(tmp, 'p'), os.path.join(repo, 'tychoc1'))
    bad = list(got.splitlines()) if got else []
    if bad:
        bad[0] = bad[0].replace('0', '1', 1)
    if err or bad == want:
        print('oracle selfcheck: FAIL -- a perturbed output still matched')
        return 1
    print('oracle selfcheck: ok (a perturbed line is detected)')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', nargs='?', type=int, default=50)
    ap.add_argument('--start', type=int, default=1)
    ap.add_argument('--jobs', type=int, default=0)
    ap.add_argument('--keep', default=None)
    ap.add_argument('--selfcheck', action='store_true')
    args = ap.parse_args()
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if args.selfcheck:
        return selfcheck(repo)
    if args.n <= 0:
        print('oracle: skipped (N=0)')
        return 0
    for compiler in ('tychoc1', 'tychoc'):
        if not os.access(os.path.join(repo, compiler), os.X_OK):
            print('missing ./%s; run make' % compiler)
            return 2
    jobs = args.jobs or max(1, (os.cpu_count() or 4) - 2)
    fails = 0
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(check_seed, seed, repo, args.keep)
                   for seed in range(args.start, args.start + args.n)]
        for fut in concurrent.futures.as_completed(futures):
            seed, compiler, detail = fut.result()
            if compiler:
                print('FAIL seed=%d compiler=%s %s' % (seed, compiler, detail))
                fails += 1
    print('oracle: %d seed(s), %d fail(s)' % (args.n, fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
