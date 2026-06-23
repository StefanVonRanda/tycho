#!/usr/bin/env python3
# Type-BOUNDARY parity: tychoc (the C reference compiler) and tychoc0 (self-hosted)
# MUST agree on whether a WELL-FORMED program type-checks (accept) or is rejected.
#
# Why this lane exists: the fixpoint differential only compares the OUTPUT of
# programs BOTH compilers accept, so a disagreement on WHETHER to accept a program
# is invisible to it -- two real type-checker divergences (int/float and char/int
# mixing) sat latent under green CI for exactly that reason. Unlike the grammar-
# boundary reject fuzzer (run_reject.py), which TOLERATES accept/reject divergence
# near malformed input, a TYPE-level disagreement on a well-formed program is
# always a bug: the two compilers must implement the same type rules.
#
# This is DETERMINISTIC and EXHAUSTIVE over the scalar binary-operator matrix --
# every (type, form) x operator x (type, form) -- not random sampling. `c := L op R`
# binds the result so the operator is type-checked; the result type is irrelevant
# to accept/reject. A program both accept must also emit C that COMPILES in both
# (an accept that emits broken C is a codegen/fail-open bug, reported too).
#
# COVERAGE: scalar types int/float/char/string/bool, each as a literal AND a
# variable, against every binary operator. NOT yet covered (a follow-up could add
# them with the same mechanism): newtypes, composite comparisons ([T]/Option/
# Result/struct ==), unary operators, and indexing/call result operands.
#
# Usage: run_typeparity.py        (no seeds -- the matrix is fixed)
import os, subprocess, sys, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
HSRC = os.path.join(REPO, "compiler", "tychoc0.ty")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
RUN_TIMEOUT = 30
BUILD_TIMEOUT = 300

# type -> (literal form, variable name declared in the prelude)
OPERANDS = {
    "int":    ("7",    "vi"),
    "float":  ("2.5",  "vf"),
    "char":   ("'A'",  "vc"),
    "string": ('"x"',  "vs"),
    "bool":   ("true", "vb"),
}
TYPES = list(OPERANDS)
OPS = ["+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^",
       "<", ">", "<=", ">=", "==", "!=", "and", "or"]
PRELUDE = ('fn main():\n'
           '    vi := 7\n    vf := 2.5\n    vc := \'A\'\n    vs := "x"\n    vb := true\n')

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

def tychoc0_verdict(h0, src, cpath):
    with open(src, "rb") as fi:
        r = subprocess.run([h0], stdin=fi, capture_output=True, timeout=RUN_TIMEOUT)
    v = classify(r.returncode, (r.stderr or b"").decode("utf-8", "replace"))
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
    b = subprocess.run(["cc", "-O2", "-std=c11", "-pthread", base + ".c", "-o", exe, "-lm"],
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
                            zv, zc = tychoc0_verdict(h0, src, os.path.join(tmp, "h0.c"))
                            if hv == "CRASH":
                                fails.append((label, "tychoc CRASH", "", prog)); continue
                            if zv == "CRASH":
                                fails.append((label, "tychoc0 CRASH", "", prog)); continue
                            if hv != zv:
                                fails.append((label, "ACCEPT/REJECT DIVERGENCE",
                                              "tychoc=%s tychoc0=%s" % (hv, zv), prog)); continue
                            if hv == "accept" and not c_compiles(hc):
                                fails.append((label, "tychoc accepted, emitted C does not compile", "", prog)); continue
                            if zv == "accept" and not c_compiles(zc):
                                fails.append((label, "tychoc0 accepted, emitted C does not compile", "", prog)); continue
        if fails:
            print("TYPE-PARITY FAIL: %d/%d cases diverge\n" % (len(fails), total))
            for i, (label, kind, detail, prog) in enumerate(fails):
                print("  [%s]  %s   %s" % (kind, label, detail))
                fn = os.path.join(FINDINGS, "typeparity_%02d.ty" % i)
                with open(fn, "w") as f:
                    f.write("# %s -- %s %s\n%s" % (kind, label, detail, prog))
            print("\nfindings saved in fuzz/findings/typeparity_*.ty")
            sys.exit(1)
        print("type-parity: %d/%d scalar binop cases AGREE (accept/reject + emitted C) -- tychoc == tychoc0" % (total, total))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
