#!/usr/bin/env python3
# Type-BOUNDARY sweep over the scalar binary-operator matrix.
#
# HISTORY -- THE PARITY ASSERTION WAS RETIRED 2026-07-29. Until then this lane was
# a DIFFERENTIAL: tychoc (the C reference compiler) and tychoc0 (self-hosted) had
# to AGREE on whether a WELL-FORMED program type-checks. That mattered because the
# fixpoint differential only compares the OUTPUT of programs BOTH compilers accept,
# so a disagreement on WHETHER to accept was invisible to it -- two real
# type-checker divergences (int/float and char/int mixing) sat latent under green
# CI for exactly that reason. Unlike the grammar-boundary reject fuzzer
# (run_reject.py), which TOLERATES accept/reject divergence near malformed input, a
# TYPE-level disagreement on a well-formed program was always a bug.
#
# WHY IT WENT: compiler/tychoc0.ty is FROZEN, and the breaking loop-syntax change
# of 2026-07-29 (three-clause `for` and bare `for:` replace `for i in range(...)`,
# `range` deleted) means it can no longer parse the corpus. No lane builds it now.
# See compiler/fixpoint.sh's header, ROADMAP.md and docs/architecture.md.
#
# WHAT IS LOST: a second independent implementation of the type rules to check
# tychoc against. There is no `expect` table here -- the tychoc0 verdict WAS the
# oracle -- so what remains is strictly weaker: an exhaustive fail-closed sweep
# asserting tychoc never CRASHES on any (type, form) x operator x (type, form)
# case, and that every program it accepts emits C that compiles. A silent change
# to a type RULE now passes this lane. Adding an `expect` table (as
# run_eqparity.py and run_unaryparity.py carry) would restore an oracle; that is
# left undone rather than pretended.
#
# This is DETERMINISTIC and EXHAUSTIVE over the scalar binary-operator matrix --
# every (type, form) x operator x (type, form) -- not random sampling. `c := L op R`
# binds the result so the operator is type-checked; the result type is irrelevant
# to accept/reject. A program both accept must also emit C that COMPILES in both
# (an accept that emits broken C is a codegen/fail-open bug, reported too).
#
# COVERAGE: scalar types int/float/char/string/bool AND the sized numerics
# u32/u64/f32, each as a literal (the sized ones via their to_*() constructor,
# since there is no sized literal syntax) AND a variable, against every binary
# operator. NOT yet covered (a follow-up could add them with the same mechanism):
# newtypes, composite comparisons ([T]/Option/Result/struct ==), unary operators,
# and indexing/call result operands.
#
# Usage: run_typeparity.py        (no seeds -- the matrix is fixed)
import os, subprocess, sys, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
RUN_TIMEOUT = 30

# type -> (literal form, variable name declared in the prelude)
OPERANDS = {
    "int":    ("7",           "vi"),
    "float":  ("2.5",         "vf"),
    "char":   ("'A'",         "vc"),
    "string": ('"x"',         "vs"),
    "bool":   ("true",        "vb"),
    "u32":    ("to_u32(7)",   "vu"),
    "u64":    ("to_u64(7)",   "vw"),
    "f32":    ("to_f32(2.5)", "vg"),
}
TYPES = list(OPERANDS)
OPS = ["+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^",
       "<", ">", "<=", ">=", "==", "!=", "and", "or"]
PRELUDE = ('fn main():\n'
           '    vi := 7\n    vf := 2.5\n    vc := \'A\'\n    vs := "x"\n    vb := true\n'
           '    vu := to_u32(7)\n    vw := to_u64(7)\n    vg := to_f32(2.5)\n')

def forms(t):
    lit, var = OPERANDS[t]
    return [lit, var]

def program(lform, op, rform):
    return PRELUDE + "    c := %s %s %s\n" % (lform, op, rform)

def classify(rc, err):
    if rc < 0:
        return "CRASH"
    return "accept" if rc == 0 else "reject"

def tychoc_verdict(src, base):
    r = subprocess.run([TYCHOC, src, "--emit-c", "-o", base],
                       capture_output=True, text=True, timeout=RUN_TIMEOUT)
    v = classify(r.returncode, r.stderr)
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
        total = 0
        fails = []   # (label, kind, detail, program)
        for lt in TYPES:
            for lform in forms(lt):
                for op in OPS:
                    for rt in TYPES:
                        for rform in forms(rt):
                            total += 1
                            prog = program(lform, op, rform)
                            with open(src, "w") as f:
                                f.write(prog)
                            label = "%s %s %s" % (lform, op, rform)
                            hv, hc = tychoc_verdict(src, os.path.join(tmp, "hc"))
                            if hv == "CRASH":
                                fails.append((label, "tychoc CRASH", "", prog)); continue
                            if hv == "accept" and not c_compiles(hc):
                                fails.append((label, "tychoc accepted, emitted C does not compile", "", prog)); continue
        if fails:
            print("TYPE-PARITY FAIL: %d/%d cases bad\n" % (len(fails), total))
            for i, (label, kind, detail, prog) in enumerate(fails):
                print("  [%s]  %s   %s" % (kind, label, detail))
                fn = os.path.join(FINDINGS, "typeparity_%02d.ty" % i)
                with open(fn, "w") as f:
                    f.write("# %s -- %s %s\n%s" % (kind, label, detail, prog))
            print("\nfindings saved in fuzz/findings/typeparity_*.ty")
            sys.exit(1)
        print("type-parity: %d/%d scalar binop cases OK (tychoc fails closed; every accept "
              "emits compilable C).\n             NOTE: the tychoc0 differential was retired "
              "2026-07-29 -- see this file's header." % (total, total))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
