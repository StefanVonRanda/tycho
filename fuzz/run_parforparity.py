#!/usr/bin/env python3
# parallel-for GATE coverage: one program per `parallel for` soundness gate, plus
# a valid baseline that exercises the SAME construct without tripping it.
# Deterministic, not random. The body of a chunk has hard rules -- no early exit
# (return / break-to-the-parfor / or_return), no in-place mutation or `inout`-pass
# of a captured variable, an outer variable may be updated ONLY as a +/* reduction
# read nowhere else, a zero-based int range -- and each rule is a SOUNDNESS
# gate: a chunk that violates one silently miscompiles (a private copy drained, a
# partial read as the whole, an early exit that can't cross a thread boundary).
#
# A case is a (name, expect, program). `expect` -- what tychoc SHOULD say -- is
# the oracle: the fixture must really trip (or really not trip) the gate. An
# accepted program must also emit C that COMPILES (an accept that emits broken C
# is a codegen/fail-open bug, reported too).
#
# HISTORY -- THE PARITY ASSERTION WAS RETIRED 2026-07-29. Until then this lane was
# also a DIFFERENTIAL: tychoc and the frozen self-hosted tychoc0 had to agree on
# every verdict. That mattered because the fixpoint differential only compared the
# OUTPUT of programs BOTH compilers accept, so a disagreement on WHETHER to accept
# was invisible to it -- and tychoc0 was found to FAIL-OPEN on return-in-parfor
# (it leaned on tychoc as the oracle and never ported the gates at all).
#
# WHY IT WENT: compiler/tychoc0.ty is FROZEN, and the breaking loop-syntax change
# of 2026-07-29 (the three-clause `for`, bare `for:` and `parallel for i in 0..<N:`
# replace the old `range()` counting header, which was deleted) means it can no
# longer parse the corpus, so no lane builds it.
# See compiler/fixpoint.sh's header, ROADMAP.md and docs/architecture.md.
#
# The PROGRAMS BELOW were left in the deleted syntax by that change and only
# rewritten 2026-07-30 (the loops-cleanup plan). Until then every one of them was
# rejected at parse: the nine accept baselines failed loudly, but the sixteen
# reject fixtures still "passed" -- rejected for saying `range()`, not for tripping
# the gate each one exists to test. This lane is not in `make ci`, so nothing said
# so. Anything added here must be run by hand.
#
# WHAT IS LOST: the ability to catch a SECOND implementation failing to enforce a
# gate. What remains -- tychoc against the written-down `expect` oracle -- is the
# half that gates the compiler people actually ship, and it is unchanged.
#
# Usage: run_parforparity.py        (no seeds -- the case set is fixed)
import os, subprocess, sys, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
RUN_TIMEOUT = 30

# ---- reject cases: each trips exactly one parallel-for gate in tychoc ----------
REJECT = {
"return": '''\
fn main():
    acc := 0
    parallel for i in 0..<10:
        if i > 3:
            return
        acc = acc + i
''',
"break_to_parfor": '''\
fn main():
    parallel for i in 0..<10:
        if i > 3:
            break
''',
"or_return": '''\
fn g(o: Option(int)) -> Option(int):
    acc := 0
    parallel for i in 0..<10:
        v := o or_return
        acc = acc + v
    return Some(acc)
fn main():
    print(str(g(Some(3))))
''',
"push_capture": '''\
fn main():
    xs := [1, 2, 3]
    parallel for i in 0..<3:
        push(xs, i)
''',
"indexset_capture": '''\
fn main():
    xs := [1, 2, 3]
    parallel for i in 0..<3:
        xs[0] = i
''',
"fieldset_capture": '''\
struct P:
    x: int
fn main():
    p := P(0)
    parallel for i in 0..<3:
        p.x = i
''',
"assign_nonreduction": '''\
fn main():
    acc := 0
    parallel for i in 0..<10:
        acc = i
''',
"reduction_bad_op": '''\
fn main():
    acc := 0
    parallel for i in 0..<10:
        acc = acc - i
''',
"read_accumulator": '''\
fn main():
    acc := 0
    parallel for i in 0..<10:
        acc = acc + i
        print(str(acc))
''',
"accumulator_two_ops": '''\
fn main():
    acc := 0
    parallel for i in 0..<10:
        acc = acc + i
        acc = acc * i
''',
# REPLACES the retired `range_step` fixture. Until 2026-07-29 the counting header
# was `range(a, b, step)` and a non-unit step was a SOUNDNESS gate (a strided space
# cannot be chunked); `parallel for i in range(0, 10, 2)` was its fixture. `0..<N`
# has no step syntax at all, so that die_at is now unreachable by construction and
# is kept only as a fail-closed assertion (src/tychoc.c:7710@r_step). The constraint that
# took its place is the LOWER bound: `0..<N` demands a literal `0`
# (src/tychoc.c:3996@i_dotlt), so this is the range-shape rejection that still has a source
# spelling. Folding the old stride into the body (`0..<5` with `i * 2`) was the
# other option and was rejected: it turns a gate fixture into a second accept
# baseline that duplicates `reduction_add` and asserts nothing.
"range_nonzero_start": '''\
fn main():
    acc := 0
    parallel for i in 1..<10:
        acc = acc + i
''',
"range_not_int": '''\
fn main():
    acc := 0
    n := 10.0
    parallel for i in 0..<n:
        acc = acc + i
''',
"range_compound_not_int": '''\
fn main():
    acc := 0
    n := 5.0
    parallel for i in 0..<n + n:
        acc = acc + i
''',
"pass_capture_as_mut": '''\
fn bump(xs: inout [int]):
    xs[0] = 9
fn main():
    xs := [1, 2, 3]
    parallel for i in 0..<3:
        bump(&xs)
''',
# The RHS of a multi-assign must be a single TUPLE-valued expression
# (src/tychoc.c:7599-7602), so this fixture's original body `a, b = b, a` was never
# valid Tycho: it died at "expected newline" in the parser and never reached the
# capture gate it names. That was invisible while the whole file died earlier still,
# on `range()`. Respelled through a tuple-returning call, it now trips
# "parallel for cannot assign to captured variable 'a'" -- the gate it exists for.
"multiassign_capture": '''\
fn swap(x: int, y: int) -> (int, int):
    return y, x
fn main():
    a := 0
    b := 0
    parallel for i in 0..<3:
        a, b = swap(a, b)
''',
"select_return": '''\
fn main():
    a := channel(int, 8)
    send(a, 1)
    parallel for i in 0..<1:
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
    parallel for i in 0..<100:
        acc = acc + i
    print(str(acc) + "\\n")
''',
# `0..<N` starts at a literal 0 (src/tychoc.c:3996@i_dotlt), so the old `range(1, 8)` is
# respelled `0..<7` with the offset folded into the body. Folding, not renumbering
# to `0..<8`: a product over a zero-based space is 0, and 0 is also what a
# reduction that silently drains a private copy produces, so `0..<8` would make the
# fixture pass for the wrong reason. `(i + 1)` keeps the answer 5040.
"reduction_mul": '''\
fn main():
    prod := 1
    parallel for i in 0..<7:
        prod = prod * (i + 1)
    print(str(prod) + "\\n")
''',
"two_accumulators": '''\
fn main():
    s := 0
    p := 1
    parallel for i in 0..<7:
        s = s + (i + 1)
        p = p * (i + 1)
    print(str(s) + " " + str(p) + "\\n")
''',
"capture_read": '''\
fn main():
    base := 5
    acc := 0
    parallel for i in 0..<10:
        acc = acc + (i + base)
    print(str(acc) + "\\n")
''',
"range_compound_int": '''\
fn main():
    m := 6
    acc := 0
    parallel for i in 0..<m + 2:
        acc = acc + i
    print(str(acc) + "\\n")
''',
"nested_break": '''\
fn main():
    acc := 0
    parallel for i in 0..<10:
        for j := 0; j < 10; j += 1:
            if j > 3:
                break
            acc = acc + j
    print(str(acc) + "\\n")
''',
"local_indexset": '''\
fn main():
    acc := 0
    parallel for i in 0..<10:
        ys := [0, 0, 0]
        ys[0] = i
        acc = acc + ys[0]
    print(str(acc) + "\\n")
''',
"pass_local_as_mut": '''\
fn bump(xs: inout [int]):
    xs[0] = 9
fn main():
    acc := 0
    parallel for i in 0..<10:
        ys := [0, 0, 0]
        bump(&ys)
        acc = acc + ys[0]
    print(str(acc) + "\\n")
''',
"select_recv": '''\
fn feed(ch: Channel(int), n: int) -> int:
    for i := 0; i < n; i += 1:
        send(ch, i)
    return n
fn main():
    a := channel(int, 16)
    t := spawn feed(a, 8)
    m := wait(t)
    sum := 0
    parallel for i in 0..<m:
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

def c_compiles(cpath):
    if not cpath or not os.path.exists(cpath) or os.path.getsize(cpath) == 0:
        return True
    r = subprocess.run(["cc", "-fsyntax-only", "-std=c11", "-w", cpath],
                       capture_output=True, text=True, timeout=RUN_TIMEOUT)
    return r.returncode == 0

def main():
    if not os.path.exists(TYCHOC):
        print("run 'make' first (no ./tychoc)"); sys.exit(2)
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    try:
        src = os.path.join(tmp, "p.ty")
        cases = ([(n, "reject", p) for n, p in REJECT.items()] +
                 [(n, "accept", p) for n, p in ACCEPT.items()])
        fails = []   # (name, kind, detail, program)
        for name, expect, prog in cases:
            with open(src, "w") as f:
                f.write(prog)
            hv, hc = tychoc_verdict(src, os.path.join(tmp, "hc"))
            if hv == "CRASH":
                fails.append((name, "tychoc CRASH", "", prog)); continue
            # The fixture must trip (or not trip) the gate as designed in the
            # ORACLE; a drifted fixture is a test bug, surfaced loudly.
            if hv != expect:
                fails.append((name, "ORACLE DIVERGENCE", "tychoc=%s expected=%s" % (hv, expect), prog)); continue
            if hv == "accept" and not c_compiles(hc):
                fails.append((name, "tychoc accepted, emitted C does not compile", "", prog)); continue
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
        print("parfor-parity: %d/%d parallel-for gate cases match the oracle "
               "(accept/reject + emitted C).\n"
               "               NOTE: the tychoc0 differential was retired 2026-07-29 -- "
               "see this file's header." % (total, total))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
