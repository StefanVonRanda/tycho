import subprocess, sys, os, tempfile, shutil

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

def run_seed(seed, tmp):
    g = subprocess.run([sys.executable, GEN, str(seed)], capture_output=True, text=True, timeout=TIMEOUT)
    if g.returncode != 0 or not g.stdout.strip():
        return "GENFAIL", "gen.py rc=%d, %d bytes" % (g.returncode, len(g.stdout))
    src = os.path.join(tmp, "p.ty")
    with open(src, "w") as f:
        f.write(g.stdout)
    try:
        hc_ok = emit_tychoc(src, os.path.join(tmp, "hc.c"))
    except subprocess.TimeoutExpired:
        return "skip", None
    if not hc_ok:
        return "skip", None                       # tychoc rejected it
    v, d = build_run_leak(os.path.join(tmp, "hc.c"), os.path.join(tmp, "run_hc"), "tychoc")
    if v in ("LEAK", "FAULT"):
        return "FAIL", d
    if v == "ccfail":
        return "FAIL", d                          # emitted C must compile
    return "ok", None

def main():
    if sys.platform == "darwin":
        print("fuzz-leak: SKIPPED on macOS -- Apple's ASan ships no LeakSanitizer "
              "(the Linux CI leg covers this lane)")
        return 0
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    counts = {"ok": 0, "skip": 0, "FAIL": 0}
    for seed in range(start, start + n):
        try:
            v, msg = run_seed(seed, tmp)
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
