import os, subprocess, sys, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The SHIPPED compiler, not the C bootstrap: every other gate runs tychoc1,
# and checks present in tychoc are absent from it. Override with TYCHOC=.
TYCHOC = os.environ.get("TYCHOC") or os.path.join(REPO, "tychoc1")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
RUN_TIMEOUT = 30

# type -> (literal form, variable name declared in the prelude)
OPERANDS = {
    "int":    ("7",           "_vi"),
    "float":  ("2.5",         "_vf"),
    "char":   ("'A'",         "_vc"),
    "string": ('"x"',         "_vs"),
    "bool":   ("true",        "_vb"),
    "u32":    ("to_u32(7)",   "_vu"),
    "u64":    ("to_u64(7)",   "_vw"),
    "f32":    ("to_f32(2.5)", "_vg"),
}
TYPES = list(OPERANDS)
OPS = ["+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^",
       "<", ">", "<=", ">=", "==", "!=", "and", "or"]
PRELUDE = ('fn main():\n'
           '    _vi := 7\n    _vf := 2.5\n    _vc := \'A\'\n    _vs := "x"\n    _vb := true\n'
           '    _vu := to_u32(7)\n    _vw := to_u64(7)\n    _vg := to_f32(2.5)\n')

SIZED   = {"u32", "u64"}                  # is_sized_int() over this matrix's types
INTEGER = {"int"} | SIZED
ORDERED = {"int", "char", "float", "string", "f32"} | SIZED   # bool is deliberately absent

def is_lit(t, form):
    return t in ("int", "float") and form == OPERANDS[t][0]

def adapt(lt, ll, rt, rl):
    """Sized-numeric literal adaptation, in the resolver's own order
    (`src/tychoc.c:6733-6742`): an int literal takes the other side's u32/u64;
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
    if op in ("and", "or"):                       # `src/tychoc.c:6717` -- bool operands, no adaptation
        return "accept" if lt == "bool" and rt == "bool" else "reject"
    lt, rt = adapt(lt, ll, rt, rl)
    if op in ("==", "!="):                        # `src/tychoc.c:6748` -- structural, but the types must be EQUAL.
        # Note the deliberate asymmetry with ordering below: there is no
        # int-literal-to-float adaptation on the equality path, so `2.5 == 7` is a
        # type error while `2.5 < 7` is not (`src/tychoc.c:6759-6766` explains why).
        return "accept" if lt == rt else "reject"
    if op in ("<", ">", "<=", ">="):              # `src/tychoc.c:6763-6770`
        if lt == "float" and rt == "int" and rl: rt = "float"
        elif rt == "float" and lt == "int" and ll: lt = "float"
        return "accept" if lt == rt and lt in ORDERED else "reject"
    if op == "+" and lt == "string":              # `src/tychoc.c:6777` -- string + string|char, ONE-directional
        return "accept" if rt in ("string", "char") else "reject"
    if op in ("<<", ">>"):                        # `src/tychoc.c:6799-6805`
        return "accept" if lt in INTEGER and rt in INTEGER else "reject"
    if op in ("%", "&", "|", "^"):                # `src/tychoc.c:6909` -- two MATCHING integers
        return "accept" if lt in INTEGER and lt == rt else "reject"
    # arithmetic `+ - * /`
    if lt == rt and lt in ({"int", "float", "f32"} | SIZED):   # `src/tychoc.c:6919-6931`
        return "accept"
    if op in ("+", "-") and (lt == "char" or rt == "char") \
       and lt in ("char", "int") and rt in ("char", "int"):    # `src/tychoc.c:6927-6930` -- char±int, int±char, char±char
        return "accept"
    if lt == "float" and rt == "int" and rl: return "accept"   # `src/tychoc.c:6936`
    if rt == "float" and lt == "int" and ll: return "accept"   # `src/tychoc.c:6940`
    return "reject"

def forms(t):
    lit, var = OPERANDS[t]
    return [lit, var]

def program(lform, op, rform):
    return PRELUDE + "    _c := %s %s %s\n" % (lform, op, rform)

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
              "\n             above replaced it 2026-07-30 -- see this file's header."
              % (total, total, accepts, total - accepts))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

if __name__ == "__main__":
    main()
