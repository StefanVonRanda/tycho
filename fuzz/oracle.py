#!/usr/bin/env python3
"""Generated programs checked against an independent Python oracle.

The existing fuzzers compare the compiler against itself (native vs ASan, or
both compilers). This one evaluates the same AST in Python with Tycho's
semantics, so a compiler that is consistently wrong still fails. Four surfaces
are covered: int arithmetic/control flow, float arithmetic, string builtins, and
array push/index/slice/sum. Floats are compared bit-for-bit after parsing the
printed decimal, not by spelling.
"""
import argparse
import concurrent.futures
import os
import random
import struct
import subprocess
import sys
import tempfile

MASK = (1 << 64) - 1

LITERALS = [0, 1, -1, 2, 3, -2, 7, -7, 13, -13, 255, 256, 65535, 65536,
            4294967296, -4294967296, 4611686018427387904, -4611686018427387904,
            9223372036854775807, -9223372036854775807]
DIVISORS = [1, 2, 3, 5, 7, -2, -3, -5, -7]
SHIFTS = [0, 1, 7, 31, 32, 62, 63, 64, 65]
ARGS = [(0, 1, -1), (7, -13, 42), (4611686018427387904, 3, -7),
        (9223372036854775807, 9223372036854775807, -9223372036854775807),
        (-9223372036854775807, 256, 65535)]

FLITERALS = [0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.5, -3.5, 7.0]
FDIVISORS = [1.0, 2.0, 3.0, -2.0, 0.5, -0.5, 7.0]
FARGS = [(0.0, 1.0, -1.0), (2.5, -3.75, 7.0), (-0.5, 100.0, 3.25),
         (7.0, -2.0, 0.5), (-0.0, -0.0, -0.0)]

STRINGS = ["", "a", "bc", "Hello", "x y", "a,b", "MiXeD", " pad "]
SARGS = [("", "a", "bc"), ("Hello", "x y", "a,b"), ("MiXeD", " pad ", "z")]


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


class IntGen:
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

    def program(self, nfuncs=8):
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


class FloatGen:
    def __init__(self, seed):
        self.r = random.Random(seed * 7919 + 17)

    def leaf(self, env):
        if env and self.r.random() < 0.6:
            return ('var', self.r.choice(sorted(env)))
        return ('flit', self.r.choice(FLITERALS))

    def expr(self, env, depth):
        if depth <= 0 or self.r.random() < 0.35:
            return self.leaf(env)
        if self.r.random() < 0.15:
            return ('fun', '-', self.expr(env, depth - 1))
        op = self.r.choice(['+', '-', '*', '/'])
        if op == '/':
            return ('fdiv', '/', self.expr(env, depth - 1), self.r.choice(FDIVISORS))
        return ('fbin', op, self.expr(env, depth - 1), self.expr(env, depth - 1))

    def program(self, nfuncs=4):
        funcs = []
        for _ in range(nfuncs):
            env = {'a', 'b', 'c'}
            first = self.expr(env, 2)
            env.add('_x')
            second = self.expr(env, 2)
            env.add('_y')
            funcs.append((first, second, self.expr(env, 2)))
        return funcs


class StringGen:
    def __init__(self, seed):
        self.r = random.Random(seed * 104729 + 31)

    def leaf(self, env):
        if env and self.r.random() < 0.55:
            return ('svar', self.r.choice(sorted(env)))
        return ('slit', self.r.choice(STRINGS))

    def expr(self, env, depth):
        if depth <= 0 or self.r.random() < 0.30:
            return self.leaf(env)
        k = self.r.random()
        if k < 0.25:
            return ('sbin', '+', self.expr(env, depth - 1), self.expr(env, depth - 1))
        if k < 0.40:
            return ('substr', self.expr(env, depth - 1),
                    self.r.randint(-2, 8), self.r.randint(-2, 8))
        if k < 0.55:
            return ('scall', 'to_upper', self.expr(env, depth - 1))
        if k < 0.70:
            return ('scall', 'to_lower', self.expr(env, depth - 1))
        if k < 0.82:
            return ('scall', 'reverse', self.expr(env, depth - 1))
        if k < 0.92:
            return ('scall', 'trim', self.expr(env, depth - 1))
        return ('srepeat', self.expr(env, depth - 1), self.r.randint(0, 3))

    def program(self, nfuncs=4):
        funcs = []
        for _ in range(nfuncs):
            env = {'a', 'b', 'c'}
            first = self.expr(env, 2)
            env.add('_x')
            funcs.append((first, self.expr(env, 2)))
        return funcs


class ArrayGen:
    def __init__(self, seed):
        self.g = IntGen(seed * 31337 + 7)

    def program(self, nfuncs=2):
        funcs = []
        for _ in range(nfuncs):
            env = {'a', 'b', 'c'}
            e0 = self.g.expr(env, 2)
            e1 = self.g.expr(env, 2)
            e2 = self.g.expr(env, 2)
            e3 = self.g.expr(env, 2)
            e4 = self.g.expr(env, 2)
            funcs.append((e0, e1, e2, e3, e4))
        return funcs


def emit_int(e):
    kind = e[0]
    if kind == 'lit':
        return str(e[1])
    if kind == 'var':
        return e[1]
    if kind == 'un':
        return '(' + e[1] + '(' + emit_int(e[2]) + '))'
    if kind == 'bin':
        return '(' + emit_int(e[2]) + ' ' + e[1] + ' ' + emit_int(e[3]) + ')'
    if kind in ('div', 'shift'):
        return '(' + emit_int(e[2]) + ' ' + e[1] + ' ' + str(e[3]) + ')'
    if kind == 'cmp':
        return '(' + emit_int(e[2]) + ' ' + e[1] + ' ' + emit_int(e[3]) + ')'
    if kind == 'not':
        return '(not ' + emit_int(e[1]) + ')'
    if kind == 'log':
        return '(' + emit_int(e[2]) + ' ' + e[1] + ' ' + emit_int(e[3]) + ')'
    raise AssertionError(kind)


def evaluate_int(e, env):
    kind = e[0]
    if kind == 'lit':
        return wrap(e[1])
    if kind == 'var':
        return env[e[1]]
    if kind == 'un':
        v = evaluate_int(e[2], env)
        return wrap(-v if e[1] == '-' else ~v)
    if kind == 'bin':
        a, b = evaluate_int(e[2], env), evaluate_int(e[3], env)
        op = e[1]
        if op == '+': return wrap(a + b)
        if op == '-': return wrap(a - b)
        if op == '*': return wrap(a * b)
        if op == '&': return wrap(a & b)
        if op == '|': return wrap(a | b)
        if op == '^': return wrap(a ^ b)
        raise AssertionError(op)
    if kind == 'div':
        a = evaluate_int(e[2], env)
        return wrap(trunc_div(a, e[3]) if e[1] == '/' else c_rem(a, e[3]))
    if kind == 'shift':
        a = evaluate_int(e[2], env)
        return wrap(a << e[3]) if e[1] == '<<' else shift_right(a, e[3])
    if kind == 'cmp':
        a, b = evaluate_int(e[2], env), evaluate_int(e[3], env)
        return {'<': a < b, '<=': a <= b, '>': a > b, '>=': a >= b,
                '==': a == b, '!=': a != b}[e[1]]
    if kind == 'not':
        return not evaluate_int(e[1], env)
    if kind == 'log':
        left = evaluate_int(e[2], env)
        return (left and evaluate_int(e[3], env)) if e[1] == 'and' \
            else (left or evaluate_int(e[3], env))
    raise AssertionError(kind)


def run_int_func(f, a, b, c):
    env = {'a': a, 'b': b, 'c': c}
    env['x'] = evaluate_int(f[0], env)
    env['y'] = evaluate_int(f[1], env)
    env['z'] = evaluate_int(f[2], env)
    if evaluate_int(f[3], env):
        env['x'] = evaluate_int(f[4], env)
    else:
        env['y'] = evaluate_int(f[5], env)
    for i in range(3):
        env['i'] = i
        env['z'] = evaluate_int(f[6], env)
    del env['i']
    return evaluate_int(f[7], env)


def emit_float(e):
    kind = e[0]
    if kind == 'flit':
        return repr(e[1])
    if kind == 'var':
        return e[1]
    if kind == 'fun':
        return '(' + e[1] + '(' + emit_float(e[2]) + '))'
    if kind == 'fbin':
        return '(' + emit_float(e[2]) + ' ' + e[1] + ' ' + emit_float(e[3]) + ')'
    if kind == 'fdiv':
        return '(' + emit_float(e[2]) + ' / ' + repr(e[3]) + ')'
    raise AssertionError(kind)


def evaluate_float(e, env):
    kind = e[0]
    if kind == 'flit':
        return e[1]
    if kind == 'var':
        return env[e[1]]
    if kind == 'fun':
        return -evaluate_float(e[2], env)
    if kind in ('fbin', 'fdiv'):
        a = evaluate_float(e[2], env)
        b = evaluate_float(e[3], env) if kind == 'fbin' else e[3]
        op = e[1]
        if op == '+': return a + b
        if op == '-': return a - b
        if op == '*': return a * b
        if op == '/': return a / b
        raise AssertionError(op)
    raise AssertionError(kind)


def run_float_func(f, a, b, c):
    env = {'a': a, 'b': b, 'c': c}
    env['_x'] = evaluate_float(f[0], env)
    env['_y'] = evaluate_float(f[1], env)
    return evaluate_float(f[2], env)


def emit_string(e):
    kind = e[0]
    if kind == 'slit':
        return '"' + e[1] + '"'
    if kind == 'svar':
        return e[1]
    if kind == 'sbin':
        return '(' + emit_string(e[2]) + ' + ' + emit_string(e[3]) + ')'
    if kind == 'substr':
        return 'substr(' + emit_string(e[1]) + ', ' + str(e[2]) + ', ' + str(e[3]) + ')'
    if kind == 'scall':
        return 'strings.' + e[1] + '(' + emit_string(e[2]) + ')'
    if kind == 'srepeat':
        return 'strings.repeat(' + emit_string(e[1]) + ', ' + str(e[2]) + ')'
    raise AssertionError(kind)


def evaluate_string(e, env):
    kind = e[0]
    if kind == 'slit':
        return e[1]
    if kind == 'svar':
        return env[e[1]]
    if kind == 'sbin':
        return evaluate_string(e[2], env) + evaluate_string(e[3], env)
    if kind == 'substr':
        s = evaluate_string(e[1], env)
        start = max(0, min(len(s), e[2]))
        stop = max(0, min(len(s), e[3]))
        return s[start:stop] if stop >= start else ''
    if kind == 'scall':
        s = evaluate_string(e[2], env)
        if e[1] == 'to_upper': return s.upper()
        if e[1] == 'to_lower': return s.lower()
        if e[1] == 'reverse': return s[::-1]
        if e[1] == 'trim': return s.strip(' \t\r\n')
        raise AssertionError(e[1])
    if kind == 'srepeat':
        return evaluate_string(e[1], env) * e[2]
    raise AssertionError(kind)


def run_string_func(f, a, b, c):
    env = {'a': a, 'b': b, 'c': c}
    env['_x'] = evaluate_string(f[0], env)
    return evaluate_string(f[1], env)


def run_array_func(f, a, b, c):
    env = {'a': a, 'b': b, 'c': c}
    xs = [evaluate_int(f[0], env), evaluate_int(f[1], env), evaluate_int(f[2], env)]
    xs.append(evaluate_int(f[3], env))
    xs[1] = evaluate_int(f[4], env)
    part = xs[1:3]
    total = 0
    for v in part:
        total = wrap(total + v)
    return wrap(wrap(total + xs[0]) + len(xs))


def uses_strings(e):
    kind = e[0]
    if kind in ('scall', 'srepeat'):
        return True
    if kind == 'sbin':
        return uses_strings(e[2]) or uses_strings(e[3])
    if kind == 'substr':
        return uses_strings(e[1])
    return False


def make_program(seed):
    ints = IntGen(seed).program()
    floats = FloatGen(seed).program()
    strings = StringGen(seed).program()
    arrays = ArrayGen(seed).program()
    out = ['package main']
    if any(uses_strings(e) for f in strings for e in f):
        out.append('import "core:strings"')
    out.append('')
    for n, f in enumerate(ints):
        out.extend([
            'fn f%d(a: int, b: int, c: int) -> int:' % n,
            '    x := ' + emit_int(f[0]),
            '    y := ' + emit_int(f[1]),
            '    z := ' + emit_int(f[2]),
            '    if ' + emit_int(f[3]) + ':',
            '        x = ' + emit_int(f[4]),
            '    else:',
            '        y = ' + emit_int(f[5]),
            '    for i := 0; i < 3; i += 1:',
            '        z = ' + emit_int(f[6]),
            '    return ' + emit_int(f[7]),
            '',
        ])
    for n, f in enumerate(floats):
        out.extend([
            'fn g%d(a: float, b: float, c: float) -> float:' % n,
            '    _x := ' + emit_float(f[0]),
            '    _y := ' + emit_float(f[1]),
            '    return ' + emit_float(f[2]),
            '',
        ])
    for n, f in enumerate(strings):
        out.extend([
            'fn s%d(a: string, b: string, c: string) -> string:' % n,
            '    _x := ' + emit_string(f[0]),
            '    return ' + emit_string(f[1]),
            '',
        ])
    for n, f in enumerate(arrays):
        out.extend([
            'fn a%d(a: int, b: int, c: int) -> int:' % n,
            '    xs := [' + emit_int(f[0]) + ', ' + emit_int(f[1]) + ', ' + emit_int(f[2]) + ']',
            '    push(xs, ' + emit_int(f[3]) + ')',
            '    xs[1] = ' + emit_int(f[4]),
            '    part := xs[1:3]',
            '    t := 0',
            '    for i := 0; i < len(part); i += 1:',
            '        t = t + part[i]',
            '    return t + xs[0] + len(xs)',
            '',
        ])
    out.append('fn main():')
    lines = []
    for n, f in enumerate(ints):
        vals = [run_int_func(f, a, b, c) for a, b, c in ARGS]
        lines.append(' '.join(str(v) for v in vals))
        parts = ['str(f%d(%d, %d, %d))' % (n, a, b, c) for a, b, c in ARGS]
        out.append('    println(' + ' + " " + '.join(parts) + ')')
    for n, f in enumerate(floats):
        vals = [run_float_func(f, a, b, c) for a, b, c in FARGS]
        lines.append(' '.join(repr(v) for v in vals))
        parts = ['str(g%d(%r, %r, %r))' % (n, a, b, c) for a, b, c in FARGS]
        out.append('    println(' + ' + " " + '.join(parts) + ')')
    for n, f in enumerate(strings):
        vals = [run_string_func(f, a, b, c) for a, b, c in SARGS]
        lines.append('|'.join(vals))
        parts = ['s%d("%s", "%s", "%s")' % (n, a, b, c) for a, b, c in SARGS]
        out.append('    println(' + ' + "|" + '.join(parts) + ')')
    for n, f in enumerate(arrays):
        vals = [run_array_func(f, a, b, c) for a, b, c in ARGS]
        lines.append(' '.join(str(v) for v in vals))
        parts = ['str(a%d(%d, %d, %d))' % (n, a, b, c) for a, b, c in ARGS]
        out.append('    println(' + ' + " " + '.join(parts) + ')')
    kinds = (['int'] * len(ints) + ['float'] * len(floats) +
             ['str'] * len(strings) + ['arr'] * len(arrays))
    return '\n'.join(out) + '\n', lines, kinds


def parse_float_line(line):
    return [struct.pack('d', float(tok)) for tok in line.split()]


def compile_run(src, base, compiler):
    r = subprocess.run([compiler, src, '--emit-c', '-o', base],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return None, '%s rc=%d: %s' % (os.path.basename(compiler), r.returncode,
                                       (r.stderr or r.stdout)[:300])
    repo = os.path.dirname(compiler)
    shim = os.path.join(repo, 'corelib', 'strings', 'strings_shim.c')
    r = subprocess.run(['cc', '-O2', '-fwrapv', '-std=c11', '-o', base + '.bin',
                        base + '.c', shim], capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return None, 'cc rc=%d: %s' % (r.returncode, r.stderr[:300])
    r = subprocess.run([base + '.bin'], capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        return None, 'run rc=%d: %s' % (r.returncode, (r.stderr or '')[:300])
    return r.stdout, None


def compare(got, want, kinds):
    got_lines = got.splitlines()
    if len(got_lines) != len(want):
        return 'line count want=%d got=%d' % (len(want), len(got_lines))
    for i, (g, w, kind) in enumerate(zip(got_lines, want, kinds)):
        if kind == 'float':
            try:
                if parse_float_line(g) != parse_float_line(w):
                    return 'line %d float bits want=%r got=%r' % (i + 1, w[:100], g[:100])
            except ValueError:
                return 'line %d is not a float: %r' % (i + 1, g[:100])
        elif g != w:
            return 'line %d want=%r got=%r' % (i + 1, w[:120], g[:120])
    return None


def check_seed(seed, repo, keep):
    src_text, want, kinds = make_program(seed)
    with tempfile.TemporaryDirectory(prefix='oracle_') as tmp:
        src = os.path.join(tmp, 'p.ty')
        with open(src, 'w') as f:
            f.write(src_text)
        for compiler in (os.path.join(repo, 'tychoc1'), os.path.join(repo, 'tychoc')):
            got, err = compile_run(src, os.path.join(tmp, os.path.basename(compiler)), compiler)
            detail = err or (compare(got, want, kinds) if got is not None else 'no output')
            if detail:
                if keep:
                    os.makedirs(keep, exist_ok=True)
                    with open(os.path.join(keep, 'seed_%d.ty' % seed), 'w') as f:
                        f.write(src_text)
                return seed, os.path.basename(compiler), detail
    return seed, None, None


def selfcheck(repo):
    src_text, want, kinds = make_program(1)
    with tempfile.TemporaryDirectory(prefix='oracle_selfcheck_') as tmp:
        src = os.path.join(tmp, 'p.ty')
        with open(src, 'w') as f:
            f.write(src_text)
        got, err = compile_run(src, os.path.join(tmp, 'p'), os.path.join(repo, 'tychoc1'))
    if err or not got:
        print('oracle selfcheck: FAIL -- %s' % err)
        return 1
    for kind in ('int', 'float', 'str', 'arr'):
        bad = list(want)
        i = kinds.index(kind)
        if kind == 'float':
            toks = bad[i].split()
            toks[0] = '0.0' if float(toks[0]) != 0.0 else '1.0'
            bad[i] = ' '.join(toks)
        else:
            bad[i] = bad[i] + 'x'
        if compare(got, bad, kinds) is None:
            print('oracle selfcheck: FAIL -- a perturbed %s line still matched' % kind)
            return 1
    print('oracle selfcheck: ok (int, float, string and array mismatches are detected)')
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
