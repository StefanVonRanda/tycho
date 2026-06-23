#!/usr/bin/env python3
# parallel-for GATE parity: tychoc (the C reference compiler) and tychoc0 (self-
# hosted) MUST agree on whether a program with a `parallel for` is accepted or
# rejected. The body of a chunk has hard rules -- no early exit (return / break-
# to-the-parfor / or_return), no in-place mutation or `mut`-pass of a captured
# variable, an outer variable may be updated ONLY as a +/* reduction read nowhere
# else, no range step, an int range -- and each rule is a SOUNDNESS gate: a chunk
# that violates one silently miscompiles (a private copy drained, a partial read
# as the whole, an early exit that can't cross a thread boundary).
#
# Why this lane exists: the fixpoint differential only compares the OUTPUT of
# programs BOTH compilers accept, so a disagreement on WHETHER to accept is
# invisible to it -- and tychoc0 was found to FAIL-OPEN on return-in-parfor (it
# leaned on tychoc as the oracle and never ported the gates). The reject-test
# harness gates the C compiler only, so it cannot catch a tychoc0 fail-open
# either. This lane closes that blind spot the same way run_typeparity.py closed
# the type-boundary one: deterministic, not random, one program per gate plus a
# valid baseline that exercises the SAME construct without tripping it.
#
# A case is a (name, expect, program). `expect` is what tychoc SHOULD say -- a
# sanity check that the fixture really trips (or really doesn't trip) the gate;
# the PARITY assertion is tychoc == tychoc0 regardless of `expect`. A program both
# accept must also emit C that COMPILES in both (an accept that emits broken C is
# a codegen/fail-open bug, reported too).
#
# Usage: run_parforparity.py        (no seeds -- the case set is fixed)
import os, subprocess, sys, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
HSRC = os.path.join(REPO, "compiler", "tychoc0.ty")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
RUN_TIMEOUT = 30
BUILD_TIMEOUT = 300

# ---- reject cases: each trips exactly one parallel-for gate in tychoc ----------
REJECT = {
"return": '''\
fn main():
    acc := 0
    parallel for i in range(10):
        if i > 3:
            return
        acc = acc + i
''',
"break_to_parfor": '''\
fn main():
    parallel for i in range(10):
        if i > 3:
            break
''',
"or_return": '''\
fn g(o: Option(int)) -> Option(int):
    acc := 0
    parallel for i in range(10):
        v := o or_return
        acc = acc + v
    return Some(acc)
fn main():
    print(str(g(Some(3))))
''',
"push_capture": '''\
fn main():
    xs := [1, 2, 3]
    parallel for i in range(3):
        push(xs, i)
''',
"indexset_capture": '''\
fn main():
    xs := [1, 2, 3]
    parallel for i in range(3):
        xs[0] = i
''',
"fieldset_capture": '''\
struct P:
    x: int
fn main():
    p := P(0)
    parallel for i in range(3):
        p.x = i
''',
"assign_nonreduction": '''\
fn main():
    acc := 0
    parallel for i in range(10):
        acc = i
''',
"reduction_bad_op": '''\
fn main():
    acc := 0
    parallel for i in range(10):
        acc = acc - i
''',
"read_accumulator": '''\
fn main():
    acc := 0
    parallel for i in range(10):
        acc = acc + i
        print(str(acc))
''',
"accumulator_two_ops": '''\
fn main():
    acc := 0
    parallel for i in range(10):
        acc = acc + i
        acc = acc * i
''',
"range_step": '''\
fn main():
    acc := 0
    parallel for i in range(0, 10, 2):
        acc = acc + i
''',
"range_not_int": '''\
fn main():
    acc := 0
    n := 10.0
    parallel for i in range(0, n):
        acc = acc + i
''',
"range_compound_not_int": '''\
fn main():
    acc := 0
    n := 5.0
    parallel for i in range(0, n + n):
        acc = acc + i
''',
"pass_capture_as_mut": '''\
fn bump(xs: mut [int]):
    xs[0] = 9
fn main():
    xs := [1, 2, 3]
    parallel for i in range(3):
        bump(&xs)
''',
"multiassign_capture": '''\
fn main():
    a := 0
    b := 0
    parallel for i in range(3):
        a, b = b, a
''',
"select_return": '''\
fn main():
    a := channel(int, 8)
    send(a, 1)
    parallel for i in range(0, 1):
        select:
            recv(a, x):
                return
    print("x\\n")
''',
}

# ---- accept baselines: the SAME constructs, used legally -----------------------
ACCEPT = {
"reduction_add": '''\
fn main():
    acc := 0
    parallel for i in range(100):
        acc = acc + i
    print(str(acc) + "\\n")
''',
"reduction_mul": '''\
fn main():
    prod := 1
    parallel for i in range(1, 8):
        prod = prod * i
    print(str(prod) + "\\n")
''',
"two_accumulators": '''\
fn main():
    s := 0
    p := 1
    parallel for i in range(1, 8):
        s = s + i
        p = p * i
    print(str(s) + " " + str(p) + "\\n")
''',
"capture_read": '''\
fn main():
    base := 5
    acc := 0
    parallel for i in range(10):
        acc = acc + (i + base)
    print(str(acc) + "\\n")
''',
"range_compound_int": '''\
fn main():
    m := 6
    acc := 0
    parallel for i in range(1, m + 2):
        acc = acc + i
    print(str(acc) + "\\n")
''',
"nested_break": '''\
fn main():
    acc := 0
    parallel for i in range(10):
        for j in range(10):
            if j > 3:
                break
            acc = acc + j
    print(str(acc) + "\\n")
''',
"local_indexset": '''\
fn main():
    acc := 0
    parallel for i in range(10):
        ys := [0, 0, 0]
        ys[0] = i
        acc = acc + ys[0]
    print(str(acc) + "\\n")
''',
"pass_local_as_mut": '''\
fn bump(xs: mut [int]):
    xs[0] = 9
fn main():
    acc := 0
    parallel for i in range(10):
        ys := [0, 0, 0]
        bump(&ys)
        acc = acc + ys[0]
    print(str(acc) + "\\n")
''',
"select_recv": '''\
fn feed(ch: Channel(int), n: int) -> int:
    for i in range(n):
        send(ch, i)
    return n
fn main():
    a := channel(int, 16)
    t := spawn feed(a, 8)
    m := wait(t)
    sum := 0
    parallel for i in range(0, m):
        select:
            recv(a, x):
                sum = sum + x
    print(str(sum) + "\\n")
    close(a)
''',
}

def classify(rc):
    if rc < 0:
        return "CRASH"
    return "accept" if rc == 0 else "reject"

def tychoc_verdict(src, base):
    r = subprocess.run([TYCHOC, src, "--emit-c", "-o", base],
                       capture_output=True, text=True, timeout=RUN_TIMEOUT)
    v = classify(r.returncode)
    c = base + ".c" if v == "accept" and os.path.exists(base + ".c") else None
    return v, c

def tychoc0_verdict(h0, src, cpath):
    with open(src, "rb") as fi:
        r = subprocess.run([h0], stdin=fi, capture_output=True, timeout=RUN_TIMEOUT)
    v = classify(r.returncode)
    if v == "accept" and r.stdout:
        with open(cpath, "wb") as fo:
            fo.write(r.stdout)
        return v, cpath
    return v, None

def c_compiles(cpath):
    if not cpath or not os.path.exists(cpath) or os.path.getsize(cpath) == 0:
        return True
    r = subprocess.run(["cc", "-fsyntax-only", "-std=c11", "-w", cpath],
                       capture_output=True, text=True, timeout=RUN_TIMEOUT)
    return r.returncode == 0

def build_tychoc0(tmp):
    base = os.path.join(tmp, "h0src")
    r = subprocess.run([TYCHOC, HSRC, "--emit-c", "-o", base],
                       capture_output=True, text=True, timeout=BUILD_TIMEOUT)
    if r.returncode != 0 or not os.path.exists(base + ".c"):
        print("FATAL: tychoc could not emit tychoc0 C:\n" + r.stderr[:1500]); sys.exit(2)
    exe = os.path.join(tmp, "tychoc0")
    b = subprocess.run(["cc", "-O2", "-fwrapv", "-std=c11", "-pthread", base + ".c", "-o", exe, "-lm"],
                       capture_output=True, text=True, timeout=BUILD_TIMEOUT)
    if b.returncode != 0:
        print("FATAL: tychoc0 C did not compile:\n" + b.stderr[:1500]); sys.exit(2)
    return exe

def main():
    if not os.path.exists(TYCHOC):
        print("run 'make' first (no ./tychoc)"); sys.exit(2)
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    try:
        h0 = build_tychoc0(tmp)
        src = os.path.join(tmp, "p.ty")
        cases = ([(n, "reject", p) for n, p in REJECT.items()] +
                 [(n, "accept", p) for n, p in ACCEPT.items()])
        fails = []   # (name, kind, detail, program)
        for name, expect, prog in cases:
            with open(src, "w") as f:
                f.write(prog)
            hv, hc = tychoc_verdict(src, os.path.join(tmp, "hc"))
            zv, zc = tychoc0_verdict(h0, src, os.path.join(tmp, "h0.c"))
            if hv == "CRASH":
                fails.append((name, "tychoc CRASH", "", prog)); continue
            if zv == "CRASH":
                fails.append((name, "tychoc0 CRASH", "", prog)); continue
            # sanity: the fixture must trip (or not trip) the gate as designed in
            # the ORACLE; a drifted fixture is a test bug, surfaced loudly.
            if hv != expect:
                fails.append((name, "FIXTURE DRIFT", "tychoc=%s expected=%s" % (hv, expect), prog)); continue
            if hv != zv:
                fails.append((name, "ACCEPT/REJECT DIVERGENCE",
                              "tychoc=%s tychoc0=%s" % (hv, zv), prog)); continue
            if hv == "accept" and not c_compiles(hc):
                fails.append((name, "tychoc accepted, emitted C does not compile", "", prog)); continue
            if zv == "accept" and not c_compiles(zc):
                fails.append((name, "tychoc0 accepted, emitted C does not compile", "", prog)); continue
        total = len(cases)
        if fails:
            print("PARFOR-PARITY FAIL: %d/%d cases diverge\n" % (len(fails), total))
            for i, (name, kind, detail, prog) in enumerate(fails):
                print("  [%s]  %s   %s" % (kind, name, detail))
                fn = os.path.join(FINDINGS, "parforparity_%s.ty" % name)
                with open(fn, "w") as f:
                    f.write("# %s -- %s %s\n%s" % (kind, name, detail, prog))
            print("\nfindings saved in fuzz/findings/parforparity_*.ty")
            sys.exit(1)
        print("parfor-parity: %d/%d parallel-for gate cases AGREE "
              "(accept/reject + emitted C) -- tychoc == tychoc0" % (total, total))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
