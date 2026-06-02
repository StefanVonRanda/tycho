#!/usr/bin/env python3
# Shrink a failing program (fuzz/findings/seed_N.hi) while preserving the
# hierc0-under-ASan use-after-free, by greedily deleting statement blocks
# (a line + its more-indented continuation). A deletion that breaks compilation
# or kills the fault is rejected, so the result stays a valid, minimal repro.
#
#   python3 fuzz/minimize.py fuzz/findings/seed_690.hi
import subprocess, sys, os, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HIERC = os.path.join(REPO, "hierc")
ASAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
TMP = tempfile.mkdtemp()
H0 = os.path.join(TMP, "h0")
subprocess.run([HIERC, os.path.join(REPO, "compiler", "hierc0.hi"), "-o", H0], check=True)

def faults(src):
    p = os.path.join(TMP, "p.hi"); c = os.path.join(TMP, "p.c"); e = os.path.join(TMP, "p")
    open(p, "w").write(src)
    with open(p) as fi, open(c, "w") as fo:
        if subprocess.run([H0], stdin=fi, stdout=fo, stderr=subprocess.DEVNULL).returncode != 0:
            return False
    if os.path.getsize(c) == 0:
        return False
    if subprocess.run(["cc", "-O1", "-std=c11"] + ASAN + [c, "-o", e], capture_output=True).returncode != 0:
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
    src = open(sys.argv[1]).read()
    assert faults(src), "input does not reproduce the fault"
    out = minimize(src.split("\n"))
    print("\n".join(out))
    sys.stderr.write("minimized to %d lines\n" % len([l for l in out if l.strip()]))
