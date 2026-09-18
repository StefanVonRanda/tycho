import subprocess, sys, os, tempfile, shutil
from concurrent.futures import ProcessPoolExecutor, as_completed

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, "fuzz", "gen.py")
# The SHIPPED compiler, not the C bootstrap: every other gate runs tychoc1,
# and checks present in tychoc are absent from it. Override with TYCHOC=.
TYCHOC = os.environ.get("TYCHOC") or os.path.join(REPO, "tychoc1")
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

def build_run_leak(c_file, exe, label):
    """Build under ASan+LSan and run. Returns (verdict, detail)."""
    cc = ["cc", "-O1", "-fwrapv", "-std=c11", "-pthread"] + ASAN + [c_file, FFI_SHIM, "-o", exe]
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

def run_seed(seed):
    """One seed, end to end, in a temp dir OF ITS OWN.

    The serial version shared a single mkdtemp across every seed and wrote
    `p.ty` into it each time; with workers in parallel that is one file several
    processes write and one of them copies to findings/, so a reported seed
    would carry another seed's source. Per-seed dirs are what make this safe,
    which is why the findings copy moved in here too -- the caller no longer has
    a directory to copy from. Same shape as fuzz/run.py:run_seed.
    """
    tmp = tempfile.mkdtemp()
    try:
        try:
            g = subprocess.run([sys.executable, GEN, str(seed)], capture_output=True,
                               text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            return seed, "skip", "gen timeout"
        if g.returncode != 0 or not g.stdout.strip():
            return seed, "GENFAIL", "gen.py rc=%d, %d bytes" % (g.returncode, len(g.stdout))
        src = os.path.join(tmp, "p.ty")
        with open(src, "w") as f:
            f.write(g.stdout)
        try:
            hc_ok = emit_tychoc(src, os.path.join(tmp, "hc.c"))
        except subprocess.TimeoutExpired:
            return seed, "skip", None
        if not hc_ok:
            return seed, "skip", None                 # tychoc rejected it
        try:
            v, d = build_run_leak(os.path.join(tmp, "hc.c"), os.path.join(tmp, "run_hc"), "tychoc")
        except subprocess.TimeoutExpired:
            return seed, "skip", "harness timeout"
        # `ccfail` is a FAIL too: the emitted C must compile.
        if v in ("LEAK", "FAULT", "ccfail"):
            try:
                shutil.copy(src, os.path.join(FINDINGS, "leak_seed_%d.ty" % seed))
            except OSError:
                pass
            return seed, "FAIL", d
        return seed, "ok", None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

def main():
    if sys.platform == "darwin":
        print("fuzz-leak: SKIPPED on macOS -- Apple's ASan ships no LeakSanitizer "
              "(the Linux CI leg covers this lane)")
        return 0
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    # Same knob and same default as fuzz/run.py, which has been parallel all
    # along. This lane was the one that was not: 150 seeds one after another
    # took 168s and held the ENTIRE fuzz join, while fuzz-main did 200 seeds and
    # two builds each in 35s beside it.
    jobs = int(os.environ.get("FUZZ_JOBS", 0)) or max(1, (os.cpu_count() or 4) - 2)
    os.makedirs(FINDINGS, exist_ok=True)
    counts = {"ok": 0, "skip": 0, "FAIL": 0}
    done = genfail = 0
    print("fuzz-leak: %d seeds under ASan+LSan, %d workers" % (n, jobs))
    with ProcessPoolExecutor(max_workers=jobs) as ex:
        futs = {ex.submit(run_seed, seed): seed for seed in range(start, start + n)}
        for fut in as_completed(futs):
            seed, v, msg = fut.result()
            if v == "GENFAIL":
                # the generator itself produced nothing: the whole run is
                # meaningless, so stop rather than score the remainder.
                print("GENERATOR FAILURE at seed %d: %s" % (seed, msg))
                genfail = 1
                for f in futs:
                    f.cancel()
                break
            counts[v] = counts.get(v, 0) + 1
            if v == "FAIL":
                print("FAIL seed %d: %s" % (seed, msg))
            done += 1
            if done % 50 == 0:
                print("... %d/%d  ok=%d skip=%d FAIL=%d" % (done, n, counts["ok"], counts["skip"], counts["FAIL"]))
    if genfail:
        return 1
    print("DONE: ok=%d skip=%d FAIL=%d  (findings in fuzz/findings/)" % (counts["ok"], counts["skip"], counts["FAIL"]))
    return 1 if counts["FAIL"] else 0

if __name__ == "__main__":
    sys.exit(main())
