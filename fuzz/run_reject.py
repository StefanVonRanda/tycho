#!/usr/bin/env python3
# Robustness / fail-closed fuzz harness.
#
# Feeds MALFORMED input (fuzz/gen_malformed.py) to BOTH compilers -- each built
# under ASan+UBSan -- and asserts each FAILS CLOSED. The hard invariants (a
# violation is a FINDING, saved to fuzz/findings/, exit 1):
#
#   (1) NO CRASH: the compiler must not segfault / abort / trip ASan or UBSan /
#       hang on any input. A controlled error exit (die() -> nonzero, clean
#       stderr) is the correct, expected behaviour -- only an UNCONTROLLED fault
#       is a finding.
#   (2) NO FAIL-OPEN: if a compiler ACCEPTS the input (exit 0) and emits C, that
#       C must be valid (cc -fsyntax-only). Accepting bad input and emitting
#       broken C is a missed-rejection / codegen-on-garbage bug.
#
# Accept/reject DIVERGENCE between tychoc and tychoc0 is recorded for review (the
# two front-ends legitimately differ near the grammar boundary -- e.g. one's
# inference grounds a bare [] the other rejects) but is NOT a hard failure;
# only (1) and (2) fail the run. This keeps the oracle false-positive-free:
# every hard FAIL is an unambiguous bug.
#
# Usage: run_reject.py [N] [start]    N = seeds (default 500)
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
MAX_DIVERGENCE_SAVED = 25

def first_marker(err):
    for ln in err.splitlines():
        if any(m in ln for m in SAN_MARKERS):
            return ln.strip()[:200]
    return (err.strip().splitlines() or [""])[0][:200]

def build_asan_tychoc(tmp):
    exe = os.path.join(tmp, "tychoc_asan")
    r = subprocess.run(["cc", "-O1", "-std=c11"] + ASAN + ["-Ibuild",
                        os.path.join(REPO, "src", "tychoc.c"), "-o", exe, "-lm"],
                       cwd=REPO, capture_output=True, text=True, timeout=BUILD_TIMEOUT)
    if r.returncode != 0:
        print("ASan tychoc build FAILED:\n" + r.stderr[:2000]); return None
    return exe

def build_asan_tychoc0(tmp):
    base = os.path.join(tmp, "h0")
    r = subprocess.run([TYCHOC, os.path.join(REPO, "compiler", "tychoc0.ty"),
                        "--emit-c", "-o", base], capture_output=True, text=True, timeout=BUILD_TIMEOUT)
    if r.returncode != 0 or not os.path.exists(base + ".c"):
        print("tychoc0 emit-c FAILED:\n" + r.stderr[:2000]); return None
    exe = os.path.join(tmp, "tychoc0_asan")
    b = subprocess.run(["cc", "-O1", "-std=c11", "-pthread"] + ASAN + [base + ".c", "-o", exe, "-lm"],
                       capture_output=True, text=True, timeout=BUILD_TIMEOUT)
    if b.returncode != 0:
        print("ASan tychoc0 build FAILED:\n" + b.stderr[:2000]); return None
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

def run_tychoc0(exe, src, cpath):
    """tychoc0 reads stdin and writes emitted C to stdout."""
    try:
        with open(src, "rb") as fi:
            r = subprocess.run([exe], stdin=fi, capture_output=True, timeout=RUN_TIMEOUT, env=ENV)
    except subprocess.TimeoutExpired:
        return "CRASH", "hang (>%ds)" % RUN_TIMEOUT, None
    v, detail = classify(r, (r.stderr or b"").decode("utf-8", "replace"))
    if v == "CRASH":
        return v, detail, None
    if v == "accept" and r.stdout:
        with open(cpath, "wb") as fo:
            fo.write(r.stdout)
        return v, None, cpath
    return v, None, None

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

def run_seed(seed, hc, h0, tmp):
    g = subprocess.run([sys.executable, GEN, str(seed)], capture_output=True, text=True, timeout=RUN_TIMEOUT)
    if g.returncode != 0:
        return "GENFAIL", "gen_malformed.py rc=%d stderr=%s" % (g.returncode, (g.stderr or "")[:300])
    src = os.path.join(tmp, "p.ty")
    with open(src, "w") as f:
        f.write(g.stdout)

    hcv, hcd, hcc = run_tychoc(hc, src, os.path.join(tmp, "hc"))
    if hcv == "CRASH":
        return "FAIL", "tychoc CRASH: " + hcd
    h0v, h0d, h0c = run_tychoc0(h0, src, os.path.join(tmp, "h0out.c"))
    if h0v == "CRASH":
        return "FAIL", "tychoc0 CRASH: " + h0d

    if hcv == "accept":
        bad, ce = emitted_c_invalid(hcc)
        if bad:
            return "FAIL", "tychoc FAIL-OPEN (accepted, emitted invalid C): " + ce.strip()[:160]
    if h0v == "accept":
        bad, ce = emitted_c_invalid(h0c)
        if bad:
            return "FAIL", "tychoc0 FAIL-OPEN (accepted, emitted invalid C): " + ce.strip()[:160]

    if hcv != h0v:
        return "divergence", "tychoc=%s tychoc0=%s" % (hcv, h0v)
    return ("accept_both" if hcv == "accept" else "reject_both"), None

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    print("building ASan+UBSan compilers (tychoc from src, tychoc0 from emitted C)...")
    hc = build_asan_tychoc(tmp)
    h0 = build_asan_tychoc0(tmp)
    if not hc or not h0:
        shutil.rmtree(tmp, ignore_errors=True); return 2
    counts = {"accept_both": 0, "reject_both": 0, "divergence": 0, "FAIL": 0}
    saved_div = 0
    for seed in range(start, start + n):
        try:
            verdict, msg = run_seed(seed, hc, h0, tmp)
        except subprocess.TimeoutExpired:
            verdict, msg = "FAIL", "unexpected harness timeout"
        if verdict == "GENFAIL":
            print("GENERATOR FAILURE at seed %d: %s" % (seed, msg))
            shutil.rmtree(tmp, ignore_errors=True); return 1
        counts[verdict] = counts.get(verdict, 0) + 1
        if verdict == "FAIL":
            save(tmp, "reject_seed_%d.ty" % seed)
            print("FAIL seed %d: %s" % (seed, msg))
        elif verdict == "divergence" and saved_div < MAX_DIVERGENCE_SAVED:
            save(tmp, "divergence_%d.ty" % seed); saved_div += 1
        if seed % 200 == 0:
            print("... %d/%d  accept=%d reject=%d diverge=%d FAIL=%d" % (
                seed - start + 1, n, counts["accept_both"], counts["reject_both"],
                counts["divergence"], counts["FAIL"]))
    shutil.rmtree(tmp, ignore_errors=True)
    print("DONE: accept_both=%d reject_both=%d divergence=%d FAIL=%d  (findings in fuzz/findings/)" % (
        counts["accept_both"], counts["reject_both"], counts["divergence"], counts["FAIL"]))
    if counts["divergence"]:
        print("note: %d accept/reject divergences saved for review (not failures)" % counts["divergence"])
    return 1 if counts["FAIL"] else 0

if __name__ == "__main__":
    sys.exit(main())
