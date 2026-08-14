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
# tychoc against. Between 2026-07-29 and 2026-07-30 there was no `expect` table
# here either -- the tychoc0 verdict WAS the oracle -- so what remained was
# strictly weaker: a fail-closed sweep asserting tychoc never CRASHES and that
# every accept emits compilable C. A silent change to a type RULE passed it.
#
# WHAT REPLACED IT (the loops-cleanup plan): the `expect` oracle below, in
# the style run_eqparity.py / run_unaryparity.py / run_parforparity.py carry. Like
# run_eqparity.py's (`accept iff the two operands have the same nominal type`) it
# is a written-down RULE rather than 4608 enumerated rows -- a table that large
# could only have been machine-recorded from the compiler, which is not an oracle
# at all, just a photograph. The rule is derived from the SPEC:
#   - arithmetic / string concat / char byte-domain  docs/spec/09-expressions.md:24-40
#   - element-wise arithmetic is not in this matrix  docs/spec/09-expressions.md:51-63
#   - comparison and ordering                        docs/spec/03-types.md:436-457
#   - bitwise (operands must match)                  docs/spec/09-expressions.md:83
#   - shift (widths need NOT match)                  docs/spec/09-expressions.md:85-104
#   - literal adaptation                             docs/spec/06-conversions.md:11-27
# and it was then RECONCILED against the resolver arm by arm; each clause below
# cites the line it encodes. Reconciliation found the spec and `src/tychoc.c`
# disagreeing in one place -- mixed-width shifts -- which was resolved in the
# COMPILER's favour: the spec sentence had lumped shift in with bitwise and
# demanded matching operands, which no version of tychoc ever implemented. The
# spec now states the shift rule separately and this oracle agrees with it.
#
# What this does and does not buy: a changed type rule now reddens this lane, and
# a fail-OPEN in tychoc alone is caught. A fail-open that the rule below shares --
# because the rule was reconciled against the same compiler -- is not. That is the
# residue of losing tychoc0 and no single-implementation oracle can remove it.
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

# ---- the `expect` oracle ------------------------------------------------------
# Only `7` and `2.5` are ADAPTABLE literal forms. `to_u32(7)` and friends are
# CALLS -- a typed value, not a literal token -- so they never adapt
# (docs/spec/06-conversions.md:13-16), and `'A'`/`"x"`/`true` do not adapt either
# (docs/spec/06-conversions.md:24).
SIZED   = {"u32", "u64"}                  # is_sized_int() over this matrix's types
INTEGER = {"int"} | SIZED
ORDERED = {"int", "char", "float", "string", "f32"} | SIZED   # bool is deliberately absent

def is_lit(t, form):
    return t in ("int", "float") and form == OPERANDS[t][0]

def adapt(lt, ll, rt, rl):
    """Sized-numeric literal adaptation, in the resolver's own order
    (`src/tychoc.c:6600-6609`): an int literal takes the other side's u32/u64;
    an int OR float literal takes the other side's f32. Value-directional --
    a typed variable never changes width."""
    if ll and lt == "int" and rt in SIZED: lt = rt
    if rl and rt == "int" and lt in SIZED: rt = lt
    if rt == "f32" and ll: lt = "f32"
    if lt == "f32" and rl: rt = "f32"
    return lt, rt

def expect(lt, lform, op, rt, rform):
    """What tychoc SHOULD say for `c := L op R`. Each clause names the resolver
    arm it encodes, so a rule change shows up here as a citation that no longer
    reads the way the code does."""
    ll, rl = is_lit(lt, lform), is_lit(rt, rform)
    if op in ("and", "or"):                       # `src/tychoc.c:6584` -- bool operands, no adaptation
        return "accept" if lt == "bool" and rt == "bool" else "reject"
    lt, rt = adapt(lt, ll, rt, rl)
    if op in ("==", "!="):                        # `src/tychoc.c:6615` -- structural, but the types must be EQUAL.
        # Note the deliberate asymmetry with ordering below: there is no
        # int-literal-to-float adaptation on the equality path, so `2.5 == 7` is a
        # type error while `2.5 < 7` is not (`src/tychoc.c:6626-6633` explains why).
        return "accept" if lt == rt else "reject"
    if op in ("<", ">", "<=", ">="):              # `src/tychoc.c:6630-6637`
        if lt == "float" and rt == "int" and rl: rt = "float"
        elif rt == "float" and lt == "int" and ll: lt = "float"
        return "accept" if lt == rt and lt in ORDERED else "reject"
    if op == "+" and lt == "string":              # `src/tychoc.c:6644` -- string + string|char, ONE-directional
        return "accept" if rt in ("string", "char") else "reject"
    if op in ("<<", ">>"):                        # `src/tychoc.c:6666-6672`
        # MIXED WIDTHS ARE ACCEPTED HERE and the result takes the LEFT operand's
        # width, because a shift COUNT has no reason to share the shifted value's
        # type. docs/spec/09-expressions.md:85-89 now states exactly this, split
        # out from the bitwise rule at docs/spec/09-expressions.md:83, which does
        # require matching operands and is enforced at `src/tychoc.c:6779`.
        # Spec and compiler agree here; nothing is filed against this clause.
        return "accept" if lt in INTEGER and rt in INTEGER else "reject"
    if op in ("%", "&", "|", "^"):                # `src/tychoc.c:6776` -- two MATCHING integers
        return "accept" if lt in INTEGER and lt == rt else "reject"
    # arithmetic `+ - * /`
    if lt == rt and lt in ({"int", "float", "f32"} | SIZED):   # `src/tychoc.c:6786-6798`
        return "accept"
    if op in ("+", "-") and (lt == "char" or rt == "char") \
       and lt in ("char", "int") and rt in ("char", "int"):    # `src/tychoc.c:6794-6797` -- char±int, int±char, char±char
        return "accept"
    if lt == "float" and rt == "int" and rl: return "accept"   # `src/tychoc.c:6803`
    if rt == "float" and lt == "int" and ll: return "accept"   # `src/tychoc.c:6807`
    return "reject"

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
        total = 0; accepts = 0
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
                            label = "%s %s %s   (%s %s %s)" % (lform, op, rform, lt, op, rt)
                            want = expect(lt, lform, op, rt, rform)
                            hv, hc = tychoc_verdict(src, os.path.join(tmp, "hc"))
                            if hv == "CRASH":
                                fails.append((label, "tychoc CRASH", "", prog)); continue
                            if hv != want:
                                fails.append((label, "ORACLE DIVERGENCE",
                                              "tychoc=%s expected=%s" % (hv, want), prog)); continue
                            if hv == "accept" and not c_compiles(hc):
                                fails.append((label, "tychoc accepted, emitted C does not compile", "", prog)); continue
                            accepts += (want == "accept")
        if fails:
            print("TYPE-PARITY FAIL: %d/%d cases bad\n" % (len(fails), total))
            for i, (label, kind, detail, prog) in enumerate(fails):
                print("  [%s]  %s   %s" % (kind, label, detail))
                fn = os.path.join(FINDINGS, "typeparity_%02d.ty" % i)
                with open(fn, "w") as f:
                    f.write("# %s -- %s %s\n%s" % (kind, label, detail, prog))
            print("\nfindings saved in fuzz/findings/typeparity_*.ty")
            sys.exit(1)
        print("type-parity: %d/%d scalar binop cases match the `expect` oracle "
              "(%d accept / %d reject;\n             every accept emits compilable C, no crash on any case)."
              "\n             NOTE: the tychoc0 differential was retired 2026-07-29 and the oracle "
              "\n             above replaced it 2026-07-30 -- see this file's header."
              % (total, total, accepts, total - accepts))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
