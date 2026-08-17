import subprocess, sys, os, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
ASAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
TMP = tempfile.mkdtemp()

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
        "             this file still works; retarget faults() at the binary and\n"
        "             fault string you are actually chasing. See the file header.\n")
    sys.exit(2)
    src = open(sys.argv[1]).read()
    assert faults(src), "input does not reproduce the fault"
    out = minimize(src.split("\n"))
    print("\n".join(out))
    sys.stderr.write("minimized to %d lines\n" % len([l for l in out if l.strip()]))
