#!/usr/bin/env python3
# RETIRED 2026-07-29. This tool no longer runs and no longer builds tychoc0.
#
# WHAT IT DID, WHILE IT RAN
# -------------------------
# Shrank a failing program (fuzz/findings/seed_N.ty) while preserving a
# tychoc0-under-ASan use-after-free, by greedily deleting statement blocks (a
# line plus its more-indented continuation). A deletion that broke compilation or
# killed the fault was rejected, so the result stayed a valid, minimal repro.
# It was a debugging aid for the tychoc0 differential lanes, never a gate.
#
#   python3 fuzz/minimize.py fuzz/findings/seed_690.ty
#
# WHY IT WAS RETIRED
# ------------------
# It drove a tychoc0-derived binary, and compiler/tychoc0.ty is FROZEN: the
# breaking loop-syntax change of 2026-07-29 (three-clause `for` and bare `for:`
# replace `for i in range(...)`) means the frozen compiler can no longer parse
# the corpus, so no lane builds it. See compiler/fixpoint.sh's header,
# ROADMAP.md and docs/architecture.md.
#
# The reduction algorithm below is left INTACT and unreferenced on purpose: it is
# compiler-agnostic and is the starting point for anyone who needs to minimize a
# tychoc repro. Point `faults()` at whatever binary and whatever fault string the
# new investigation needs.
import subprocess, sys, os, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
ASAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
TMP = tempfile.mkdtemp()
H0 = os.path.join(TMP, "h0")   # was: a tychoc0 built here. No lane builds one now.

def faults(src):
    p = os.path.join(TMP, "p.ty"); c = os.path.join(TMP, "p.c"); e = os.path.join(TMP, "p")
    open(p, "w").write(src)
    with open(p) as fi, open(c, "w") as fo:
        if subprocess.run([H0], stdin=fi, stdout=fo, stderr=subprocess.DEVNULL).returncode != 0:
            return False
    if os.path.getsize(c) == 0:
        return False
    if subprocess.run(["cc", "-O1", "-fwrapv", "-std=c11"] + ASAN + [c, "-o", e], capture_output=True).returncode != 0:
        return False
    try:
        r = subprocess.run([e], capture_output=True, text=True, env=ENV, timeout=15)
    except subprocess.TimeoutExpired:
        return False
    return "use-after-free" in r.stderr   # the specific fault (not stack-overflow / other)

def indent(line):
    return len(line) - len(line.lstrip())

def minimize(lines):
    changed = True
    while changed:
        changed = False
        i = 0
        while i < len(lines):
            if lines[i].strip() == "":
                i += 1; continue
            ind = indent(lines[i])
            j = i + 1
            while j < len(lines) and (lines[j].strip() == "" or indent(lines[j]) > ind):
                j += 1
            cand = lines[:i] + lines[j:]
            if faults("\n".join(cand) + "\n"):
                lines = cand; changed = True
            else:
                i += 1
    return lines

if __name__ == "__main__":
    sys.stderr.write(
        "minimize.py: RETIRED 2026-07-29 -- it drove a tychoc0-derived binary and\n"
        "             no lane builds tychoc0 any more. The reduction algorithm in\n"
        "             this file still works; retarget faults() at the binary and\n"
        "             fault string you are actually chasing. See the file header.\n")
    sys.exit(2)
    src = open(sys.argv[1]).read()
    assert faults(src), "input does not reproduce the fault"
    out = minimize(src.split("\n"))
    print("\n".join(out))
    sys.stderr.write("minimized to %d lines\n" % len([l for l in out if l.strip()]))
