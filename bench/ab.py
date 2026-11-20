#!/usr/bin/env python3
# make bench-ab: interleaved A/B of ./tychoc1 against a BASE compiler on this machine.
# BASE=<rev> (default HEAD if the tree is dirty, else HEAD~1), built once in a git
# worktree and cached as build/ab/<sha>/. NEW_TYCHOC=/BASE_TYCHOC= compare two
# prebuilt compilers. RUNS= pairs (default 7, min 3). FAIL: median NEW/BASE > 1.05 AND
# 2nd-fastest NEW / 2nd-slowest BASE > 1.05 too: the slowdown beats the noise even
# with one outlier allowed on each side.
import os, shutil, statistics, subprocess as sp, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCHES = ["prongB/arr_pipeline", "prongB/iter_transform", "prongB/dispatch",
           "prongB/string_pipe", "prongB/binary_trees", "prongB/maptree",
           "window/window_int", "dijkstra/dijkstra", "winagg/winagg_count",
           "invindex/invindex_exact", "conc/mandelbrot", "lru/lru", "gcscan/gcscan",
           "latency/latency", "interp/interp", "conc/parreduce"]
RUNS = int(os.environ.get("RUNS", "7"))
git = lambda *a: sp.run(["git", "-C", str(ROOT), *a], check=True,
                        capture_output=True, text=True).stdout.strip()

def base_compiler():
    if os.environ.get("BASE_TYCHOC"):
        return Path(os.environ["BASE_TYCHOC"]).resolve()
    dirty = sp.run(["git", "-C", str(ROOT), "diff", "--quiet", "HEAD"]).returncode
    rev = os.environ.get("BASE") or ("HEAD" if dirty else "HEAD~1")
    sha = git("rev-parse", rev + "^{commit}")
    cache = ROOT / "build" / "ab" / sha
    if not (cache / "tychoc1").exists():
        print(f"bench-ab: building base {sha[:10]} ({rev}) in a worktree ...", flush=True)
        wt = Path(tempfile.mkdtemp(prefix="tycho-ab-"))
        try:
            git("worktree", "add", "--detach", "--quiet", str(wt), sha)
            sp.run(["make", "-s", "-C", str(wt), "tychoc1"], check=True, stdout=sp.DEVNULL)
            tmp = cache.with_suffix(".tmp")
            shutil.rmtree(tmp, ignore_errors=True)
            for d in ("corelib", "runtime"):   # tychoc1 reads both beside itself
                shutil.copytree(wt / d, tmp / d)
            shutil.copy2(wt / "tychoc1", tmp / "tychoc1")
            tmp.rename(cache)
        finally:
            sp.run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(wt)],
                   capture_output=True)
            shutil.rmtree(wt, ignore_errors=True)
    return cache / "tychoc1"

def build(tyc, name, out):
    # --runtime: the default path is cwd-relative, so BASE would silently get OUR runtime.
    rt = tyc.parent / "runtime" / "tycho_rt.c"
    env = {k: v for k, v in os.environ.items() if k not in ("TYCHO_CFLAGS", "TYCHO_CORELIB")}
    sp.run([str(tyc), str(ROOT / "bench" / f"{name}.ty"), "-o", str(out),
            *(["--runtime", str(rt)] if rt.exists() else [])], check=True, env=env, cwd=ROOT, stdout=sp.DEVNULL)

def run(b, k=1):   # mean of k back-to-back runs: a 5 ms bench is mostly exec jitter
    t = time.perf_counter()
    for _ in range(k):
        out = sp.run([str(b)], capture_output=True, check=True).stdout
    return (time.perf_counter() - t) / k, out

if sys.platform.startswith(("win", "msys", "cygwin")):
    print("bench-ab: SKIP (Windows: see bench/guard.sh)")
    sys.exit(0)
new = Path(os.environ.get("NEW_TYCHOC") or ROOT / "tychoc1").resolve()
base = base_compiler()
print(f"bench-ab: NEW {new}\n          BASE {base}\n          {RUNS} interleaved pairs per bench")
print(f"{'bench':26s} {'base ms':>9s} {'new ms':>9s} {'ratio':>6s}  verdict")
bad = 0
with tempfile.TemporaryDirectory() as T:
    for name in BENCHES:
        b, n = Path(T) / "b", Path(T) / "n"
        build(base, name, b); build(new, name, n)
        ob, on = run(b)[1], run(n)[1]       # warm-up (macOS scans a new binary) + output check
        k = max(1, round(0.2 / run(b)[0]))  # ~200 ms per sample
        tb, tn = [], []
        for i in range(RUNS):                              # alternate who goes first
            for which in ((b, n) if i % 2 == 0 else (n, b)):
                (tb if which == b else tn).append(run(which, k)[0])
        mb, mn = statistics.median(tb), statistics.median(tn)
        r = mn / mb
        v = ("FAIL output differs" if ob != on else
             "FAIL" if r > 1.05 and sorted(tn)[1] / sorted(tb)[-2] > 1.05 else "ok")
        bad += v != "ok"
        print(f"{name:26s} {mb*1000:9.1f} {mn*1000:9.1f} {r:6.3f}  {v}", flush=True)
print(f"bench-ab: {'FAILED' if bad else 'ok'} ({bad} of {len(BENCHES)} slower beyond noise)")
sys.exit(1 if bad else 0)
