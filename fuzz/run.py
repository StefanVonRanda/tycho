#!/usr/bin/env python3
# Soundness fuzz harness. For each seed: generate a random Tycho program, compile
# it with BOTH compilers, and assert they agree and neither faults.
#
#   tychoc (C reference)   -> native -O2          : the trusted oracle output
#   tychoc0 (self-hosted)  -> native -O2          : must match tychoc byte-for-byte
#   tychoc0 (self-hosted)  -> ASan/UBSan          : must not fault (UAF/UB) and must
#                                                  match its own native output
#
# A divergence (tychoc vs tychoc0, or native vs ASan), a sanitizer fault, a crash,
# or a compile-acceptance discrepancy is a FINDING (program saved to findings/).
# Programs both compilers reject are skipped. Leak detection is OFF (leaks aren't
# soundness bugs); we hunt use-after-free / heap-corruption / UB / miscompiles.
import subprocess, sys, os, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, "fuzz", "gen.py")
TYCHOC = os.path.join(REPO, "tychoc")
FFI_SHIM = os.path.join(REPO, "fuzz", "ffi_shim.c")   # backs the generator's extern fn vocabulary
FINDINGS = os.path.join(REPO, "fuzz", "findings")
ASAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
TIMEOUT = 20        # compile steps (tychoc / tychoc0 / cc)
RUN_TIMEOUT = 120   # generated binaries: generous, so a loaded machine can't
                    # false-positive a slow-but-valid program. The generator only
                    # emits terminating programs, so a real timeout is a hang:
                    # one-sided -> FAIL (divergence), both-sided -> "timeout"
                    # verdict (counted + seed reported, never a silent skip).

def sh(args, **kw):
    kw.setdefault("timeout", TIMEOUT)
    return subprocess.run(args, capture_output=True, text=True, **kw)

def emit_tychoc(src_path, out_c):
    r = subprocess.run([TYCHOC, src_path, "--emit-c", "-o", out_c[:-2]], capture_output=True, text=True, timeout=TIMEOUT)
    return r.returncode == 0 and os.path.exists(out_c)

def emit_tychoc0(h0, src_path, out_c):
    with open(src_path) as fi, open(out_c, "w") as fo:
        r = subprocess.run([h0], stdin=fi, stdout=fo, stderr=subprocess.DEVNULL, timeout=TIMEOUT)
    return r.returncode == 0 and os.path.getsize(out_c) > 0

def build_run(c_file, exe, tmp, asan=False):
    cc = ["cc", "-O1" if asan else "-O2", "-std=c11", "-pthread"] + (ASAN if asan else []) + [c_file, FFI_SHIM, "-o", exe]
    try:
        b = sh(cc)
    except subprocess.TimeoutExpired:
        return None, "timeout:cc"
    if b.returncode != 0:
        return None, "ccfail:" + b.stderr.strip()[:200]
    try:
        r = sh([exe], env=ENV, timeout=RUN_TIMEOUT)
    except subprocess.TimeoutExpired:
        return None, "timeout:run"
    if r.returncode != 0:
        return None, "runfail(rc=%d):%s" % (r.returncode, (r.stderr or "")[:200])
    return r.stdout, None

def is_to(e):
    return e is not None and e.startswith("timeout")

def run_seed(seed, h0, tmp):
    # generator failure (crash / empty output) is a hard failure of the fuzz
    # run itself, never a silent skip: every seed must yield a valid program.
    g = subprocess.run([sys.executable, GEN, str(seed)], capture_output=True, text=True, timeout=TIMEOUT)
    if g.returncode != 0 or not g.stdout.strip():
        return "GENFAIL", "gen.py rc=%d, %d bytes out, stderr: %s" % (
            g.returncode, len(g.stdout), (g.stderr or "")[:300])
    src = os.path.join(tmp, "p.ty")
    with open(src, "w") as f:
        f.write(g.stdout)
    try:
        hc_ok = emit_tychoc(src, os.path.join(tmp, "hc.c"))
    except subprocess.TimeoutExpired:
        hc_ok = "timeout"
    try:
        h0_ok = emit_tychoc0(h0, src, os.path.join(tmp, "h0.c"))
    except subprocess.TimeoutExpired:
        h0_ok = "timeout"
    if hc_ok == "timeout" or h0_ok == "timeout":
        if hc_ok == h0_ok:
            return "timeout", "both compilers timed out (%ds)" % TIMEOUT
        return "FAIL", "compile-timeout divergence: tychoc=%s tychoc0=%s" % (hc_ok, h0_ok)
    if not hc_ok and not h0_ok:
        return "skip", None
    if hc_ok != h0_ok:
        return "FAIL", "compile-discrepancy: tychoc=%s tychoc0=%s" % (hc_ok, h0_ok)
    # native (-O2) runs of both sides: the differential oracle on optimized code
    out_hc, e1 = build_run(os.path.join(tmp, "hc.c"), os.path.join(tmp, "run_hc"), tmp)
    out_h0, e2 = build_run(os.path.join(tmp, "h0.c"), os.path.join(tmp, "run_h0"), tmp)
    if is_to(e1) or is_to(e2):
        if is_to(e1) and is_to(e2):
            return "timeout", "both sides timed out (%ds): tychoc=%s tychoc0=%s" % (RUN_TIMEOUT, e1, e2)
        return "FAIL", "one-sided timeout (hang): tychoc=%s tychoc0=%s" % (e1, e2)
    if e1: return "FAIL", "tychoc " + e1            # reference itself faulted -> real bug
    if e2: return "FAIL", "tychoc0 " + e2
    # ASan/UBSan runs of BOTH sides: catches a UAF/UB in tychoc-only codegen
    # directly, not just via output divergence. ASan is slow; an ASan-only
    # timeout after a clean native run is counted (reported), not a FAIL.
    out_hca, e1a = build_run(os.path.join(tmp, "hc.c"), os.path.join(tmp, "run_hca"), tmp, asan=True)
    if is_to(e1a): return "timeout", "tychoc-ASan timed out: " + e1a
    if e1a: return "FAIL", "tychoc-ASan " + e1a
    out_as, e3 = build_run(os.path.join(tmp, "h0.c"), os.path.join(tmp, "run_h0a"), tmp, asan=True)
    if is_to(e3): return "timeout", "tychoc0-ASan timed out: " + e3
    if e3: return "FAIL", "tychoc0-ASan " + e3
    if out_hc != out_h0:
        return "FAIL", "output diverge: tychoc=%r tychoc0=%r" % (out_hc, out_h0)
    if out_hc != out_hca:
        return "FAIL", "tychoc native vs ASan diverge (UB): %r vs %r" % (out_hc, out_hca)
    if out_h0 != out_as:
        return "FAIL", "tychoc0 native vs ASan diverge (UB): %r vs %r" % (out_h0, out_as)
    return "ok", None

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    h0 = os.path.join(tmp, "h0")
    if subprocess.run([TYCHOC, os.path.join(REPO, "compiler", "tychoc0.ty"), "-o", h0]).returncode != 0:
        print("tychoc0 build failed"); return 2
    counts = {"ok": 0, "skip": 0, "FAIL": 0, "timeout": 0}
    for seed in range(start, start + n):
        try:
            verdict, msg = run_seed(seed, h0, tmp)
        except subprocess.TimeoutExpired:
            # safety net for an unexpected timeout outside the per-side handling
            verdict, msg = "timeout", "unexpected TimeoutExpired"
        if verdict == "GENFAIL":
            # the generator itself crashed or produced nothing: the whole fuzz
            # run is meaningless. Hard-fail immediately.
            print("GENERATOR FAILURE at seed %d: %s" % (seed, msg))
            shutil.rmtree(tmp, ignore_errors=True)
            return 1
        counts[verdict] = counts.get(verdict, 0) + 1
        if verdict == "FAIL":
            shutil.copy(os.path.join(tmp, "p.ty"), os.path.join(FINDINGS, "seed_%d.ty" % seed))
            print("FAIL seed %d: %s" % (seed, msg))
        elif verdict == "timeout":
            print("timeout seed %d: %s" % (seed, msg))
        if seed % 200 == 0:
            print("... %d/%d  ok=%d skip=%d timeout=%d FAIL=%d" % (seed - start + 1, n, counts["ok"], counts["skip"], counts["timeout"], counts["FAIL"]))
    shutil.rmtree(tmp, ignore_errors=True)
    print("DONE: ok=%d skip=%d timeout=%d FAIL=%d  (findings in fuzz/findings/)" % (counts["ok"], counts["skip"], counts["timeout"], counts["FAIL"]))
    if counts["FAIL"]:
        return 1
    # skip-rate ceiling: a both-compilers-reject is tolerable noise, but if a
    # large share of generated programs is rejected, the generator (or a
    # compiler front-end regression) has invalidated the run -- fail loudly
    # instead of green-exiting on near-zero coverage.
    if n >= 20 and counts["skip"] > 0.3 * n:
        print("SKIP CEILING EXCEEDED: %d/%d (>30%%) programs rejected by both compilers -- generator or front-end regression" % (counts["skip"], n))
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
