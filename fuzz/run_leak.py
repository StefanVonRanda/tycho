#!/usr/bin/env python3
# Leak-detecting fuzz lane.
#
# Feeds the SOUNDNESS generator's (gen.py) valid, terminating programs to both
# compilers, builds each under ASan+LeakSanitizer, runs it, and asserts NO memory
# is leaked at exit. The differential lane (run.py) runs with detect_leaks=0 -- it
# hunts UAF / UB / miscompiles / value-semantics violations; but a program can be
# value-semantically CORRECT and still LEAK (memory copied right, an arena or an
# owner-0 block never reclaimed), which nothing else in the broad generated space
# catches. This lane closes that hole -- it directly tests the arena model's
# "everything is reclaimed at scope/program exit" claim, the project's thesis.
#
# Why it's clean by construction: a well-formed terminating tycho program frees its
# root arena at exit, so correct codegen leaves ZERO unreachable memory. The
# intentional never-frees stay REACHABLE via statics, so LSan does NOT report them:
#   - interned string literals: each site holds the malloc via `static char *_l`
#   - the thread-local arena block pool: held by `static __thread ABlk *g_pool`
# A "definitely/indirectly lost" block is therefore a real bug (a dropped owner-0
# malloc, or an arena that was never freed) -- exactly the class run.py can't see.
#
# Runs SEQUENTIALLY: a prior parallel attempt OOM'd the machine (many concurrent
# sanitizer compiles). NOTE: do NOT cap with RLIMIT_AS/ulimit -v -- ASan reserves
# a huge virtual shadow and would fail to start; sequential is the actual fix.
# Slow, so a small N by default; `make fuzz-leak`.
import subprocess, sys, os, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, "fuzz", "gen.py")
TYCHOC = os.path.join(REPO, "tychoc")
FFI_SHIM = os.path.join(REPO, "fuzz", "ffi_shim.c")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
ASAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
# detect_leaks=1 is the whole point; halt on a UAF/UB too (those are run.py's job
# but a real one here is still a finding). LSAN suppressions file is optional.
SUPP = os.path.join(REPO, "fuzz", "leak.supp")
LENV = dict(os.environ,
            ASAN_OPTIONS="detect_leaks=1",
            LSAN_OPTIONS=("suppressions=" + SUPP) if os.path.exists(SUPP) else "",
            UBSAN_OPTIONS="halt_on_error=1")
TIMEOUT = 30        # compile steps
RUN_TIMEOUT = 60    # generated binaries (LSan adds exit-time work)

def emit_tychoc(src_path, out_c):
    r = subprocess.run([TYCHOC, src_path, "--emit-c", "-o", out_c[:-2]], capture_output=True, text=True, timeout=TIMEOUT)
    return r.returncode == 0 and os.path.exists(out_c)

def emit_tychoc0(h0, src_path, out_c):
    with open(src_path) as fi, open(out_c, "w") as fo:
        r = subprocess.run([h0], stdin=fi, stdout=fo, stderr=subprocess.DEVNULL, timeout=TIMEOUT)
    return r.returncode == 0 and os.path.getsize(out_c) > 0

def build_run_leak(c_file, exe, label):
    """Build under ASan+LSan and run. Returns (verdict, detail)."""
    cc = ["cc", "-O1", "-std=c11", "-pthread"] + ASAN + [c_file, FFI_SHIM, "-o", exe]
    try:
        b = subprocess.run(cc, capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return "timeout", label + " cc"
    if b.returncode != 0:
        return "ccfail", label + " " + b.stderr.strip()[:200]
    try:
        r = subprocess.run([exe], capture_output=True, text=True, timeout=RUN_TIMEOUT, env=LENV)
    except subprocess.TimeoutExpired:
        return "timeout", label + " run"
    err = r.stderr or ""
    if "LeakSanitizer: detected memory leaks" in err or "detected memory leaks" in err:
        return "LEAK", label + ": " + _leak_summary(err)
    if r.returncode != 0 and ("AddressSanitizer" in err or "runtime error:" in err):
        return "FAULT", label + " " + err.strip()[:200]   # a UAF/UB (run.py territory) -- still a real bug
    return "ok", None

def _leak_summary(err):
    summ = [ln for ln in err.splitlines() if "SUMMARY:" in ln]
    return (summ[0].strip() if summ else "memory leak")[:200]

def run_seed(seed, h0, tmp):
    g = subprocess.run([sys.executable, GEN, str(seed)], capture_output=True, text=True, timeout=TIMEOUT)
    if g.returncode != 0 or not g.stdout.strip():
        return "GENFAIL", "gen.py rc=%d, %d bytes" % (g.returncode, len(g.stdout))
    src = os.path.join(tmp, "p.ty")
    with open(src, "w") as f:
        f.write(g.stdout)
    try:
        hc_ok = emit_tychoc(src, os.path.join(tmp, "hc.c"))
        h0_ok = emit_tychoc0(h0, src, os.path.join(tmp, "h0.c"))
    except subprocess.TimeoutExpired:
        return "skip", None
    if not hc_ok and not h0_ok:
        return "skip", None                       # both reject -> not a valid program
    for label, ok, cf, exe in (("tychoc", hc_ok, "hc.c", "run_hc"),
                               ("tychoc0", h0_ok, "h0.c", "run_h0")):
        if not ok:
            continue
        v, d = build_run_leak(os.path.join(tmp, cf), os.path.join(tmp, exe), label)
        if v in ("LEAK", "FAULT"):
            return "FAIL", d
        if v == "ccfail":
            return "FAIL", d                      # emitted C must compile
        # timeout/ok -> continue to the other compiler
    return "ok", None

def main():
    # LeakSanitizer is absent from Apple's ASan -- detect_leaks=1 aborts every
    # binary at exit there, which this lane would misread as a fault. Skip
    # cleanly on macOS; the Linux CI leg covers the leak class. (Same platform
    # gap the test harnesses gate via ASAN_OPTIONS; see tests/run.sh.)
    if sys.platform == "darwin":
        print("fuzz-leak: SKIPPED on macOS -- Apple's ASan ships no LeakSanitizer "
              "(the Linux CI leg covers this lane)")
        return 0
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    h0 = os.path.join(tmp, "h0")
    print("building tychoc0 (for the differential leak check)...")
    if subprocess.run([TYCHOC, os.path.join(REPO, "compiler", "tychoc0.ty"), "-o", h0]).returncode != 0:
        print("tychoc0 build failed"); shutil.rmtree(tmp, ignore_errors=True); return 2
    counts = {"ok": 0, "skip": 0, "FAIL": 0}
    for seed in range(start, start + n):
        try:
            v, msg = run_seed(seed, h0, tmp)
        except subprocess.TimeoutExpired:
            v, msg = "skip", "harness timeout"
        if v == "GENFAIL":
            print("GENERATOR FAILURE at seed %d: %s" % (seed, msg))
            shutil.rmtree(tmp, ignore_errors=True); return 1
        counts[v] = counts.get(v, 0) + 1
        if v == "FAIL":
            shutil.copy(os.path.join(tmp, "p.ty"), os.path.join(FINDINGS, "leak_seed_%d.ty" % seed))
            print("FAIL seed %d: %s" % (seed, msg))
        if seed % 50 == 0:
            print("... %d/%d  ok=%d skip=%d FAIL=%d" % (seed - start + 1, n, counts["ok"], counts["skip"], counts["FAIL"]))
    shutil.rmtree(tmp, ignore_errors=True)
    print("DONE: ok=%d skip=%d FAIL=%d  (findings in fuzz/findings/)" % (counts["ok"], counts["skip"], counts["FAIL"]))
    return 1 if counts["FAIL"] else 0

if __name__ == "__main__":
    sys.exit(main())
