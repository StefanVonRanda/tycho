import subprocess, sys, os, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, "fuzz", "gen_malformed.py")
TYCHOC = os.path.join(REPO, "tychoc")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
ASAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=1",
           UBSAN_OPTIONS="halt_on_error=1:abort_on_error=1:print_stacktrace=0")
BUILD_TIMEOUT = 240   # building an ASan compiler
RUN_TIMEOUT = 15      # a compile of a tiny malformed input is sub-second; 15s == a hang
SYN_TIMEOUT = 30      # cc -fsyntax-only on emitted C (deep-nesting can balloon it)
SAN_MARKERS = ("AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:",
               "SUMMARY: ", "LeakSanitizer", "stack-overflow", "SEGV", "ERROR: ")

def first_marker(err):
    for ln in err.splitlines():
        if any(m in ln for m in SAN_MARKERS):
            return ln.strip()[:200]
    return (err.strip().splitlines() or [""])[0][:200]

def build_asan_tychoc(tmp):
    exe = os.path.join(tmp, "tychoc_asan")
    r = subprocess.run(["cc", "-O1", "-fwrapv", "-std=c11"] + ASAN + ["-Ibuild",
                        os.path.join(REPO, "src", "tychoc.c"), "-o", exe, "-lm"],
                       cwd=REPO, capture_output=True, text=True, timeout=BUILD_TIMEOUT)
    if r.returncode != 0:
        print("ASan tychoc build FAILED:\n" + r.stderr[:2000]); return None
    return exe

def classify(r, err):
    """('CRASH', detail) | 'accept' | 'reject' from a finished subprocess result."""
    if r.returncode < 0:                          # killed by a signal (SEGV/ABRT/...)
        return "CRASH", "signal %d (%s)" % (-r.returncode, first_marker(err))
    if any(m in err for m in SAN_MARKERS):
        return "CRASH", "sanitizer: " + first_marker(err)
    return ("accept" if r.returncode == 0 else "reject"), None

def run_tychoc(exe, src, base):
    """tychoc reads a file path; --emit-c -o <base> writes <base>.c on accept."""
    try:
        r = subprocess.run([exe, src, "--emit-c", "-o", base],
                           capture_output=True, timeout=RUN_TIMEOUT, env=ENV)
    except subprocess.TimeoutExpired:
        return "CRASH", "hang (>%ds)" % RUN_TIMEOUT, None
    v, detail = classify(r, (r.stderr or b"").decode("utf-8", "replace"))
    if v == "CRASH":
        return v, detail, None
    return v, None, (base + ".c" if v == "accept" and os.path.exists(base + ".c") else None)

def emitted_c_invalid(cpath):
    """True (+stderr) if emitted C fails to compile -- a fail-open bug. Empty/none -> ok."""
    if not cpath or not os.path.exists(cpath) or os.path.getsize(cpath) == 0:
        return False, ""
    try:
        r = subprocess.run(["cc", "-fsyntax-only", "-std=c11", "-w", cpath],
                           capture_output=True, text=True, timeout=SYN_TIMEOUT)
    except subprocess.TimeoutExpired:
        return False, ""    # a slow syntax check is not itself a fail-open bug
    return r.returncode != 0, r.stderr[:300]

def save(tmp, name):
    shutil.copy(os.path.join(tmp, "p.ty"), os.path.join(FINDINGS, name))

def run_seed(seed, hc, tmp):
    g = subprocess.run([sys.executable, GEN, str(seed)], capture_output=True, text=True, timeout=RUN_TIMEOUT)
    if g.returncode != 0:
        return "GENFAIL", "gen_malformed.py rc=%d stderr=%s" % (g.returncode, (g.stderr or "")[:300])
    src = os.path.join(tmp, "p.ty")
    with open(src, "w") as f:
        f.write(g.stdout)

    hcv, hcd, hcc = run_tychoc(hc, src, os.path.join(tmp, "hc"))
    if hcv == "CRASH":
        return "FAIL", "tychoc CRASH: " + hcd

    if hcv == "accept":
        bad, ce = emitted_c_invalid(hcc)
        if bad:
            return "FAIL", "tychoc FAIL-OPEN (accepted, emitted invalid C): " + ce.strip()[:160]

    return ("accepted" if hcv == "accept" else "rejected"), None

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    print("building the ASan+UBSan tychoc from src/tychoc.c...")
    hc = build_asan_tychoc(tmp)
    if not hc:
        shutil.rmtree(tmp, ignore_errors=True); return 2
    counts = {"accepted": 0, "rejected": 0, "FAIL": 0}
    for seed in range(start, start + n):
        try:
            verdict, msg = run_seed(seed, hc, tmp)
        except subprocess.TimeoutExpired:
            verdict, msg = "FAIL", "unexpected harness timeout"
        if verdict == "GENFAIL":
            print("GENERATOR FAILURE at seed %d: %s" % (seed, msg))
            shutil.rmtree(tmp, ignore_errors=True); return 1
        counts[verdict] = counts.get(verdict, 0) + 1
        if verdict == "FAIL":
            save(tmp, "reject_seed_%d.ty" % seed)
            print("FAIL seed %d: %s" % (seed, msg))
        if seed % 200 == 0:
            print("... %d/%d  accepted=%d rejected=%d FAIL=%d" % (
                seed - start + 1, n, counts["accepted"], counts["rejected"], counts["FAIL"]))
    shutil.rmtree(tmp, ignore_errors=True)
    print("DONE: accepted=%d rejected=%d FAIL=%d  (findings in fuzz/findings/)" % (
        counts["accepted"], counts["rejected"], counts["FAIL"]))
    return 1 if counts["FAIL"] else 0

if __name__ == "__main__":
    sys.exit(main())
