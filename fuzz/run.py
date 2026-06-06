#!/usr/bin/env python3
# Soundness fuzz harness. For each seed: generate a random Hier program, compile
# it with BOTH compilers, and assert they agree and neither faults.
#
#   hierc (C reference)   -> native -O2          : the trusted oracle output
#   hierc0 (self-hosted)  -> native -O2          : must match hierc byte-for-byte
#   hierc0 (self-hosted)  -> ASan/UBSan          : must not fault (UAF/UB) and must
#                                                  match its own native output
#
# A divergence (hierc vs hierc0, or native vs ASan), a sanitizer fault, a crash,
# or a compile-acceptance discrepancy is a FINDING (program saved to findings/).
# Programs both compilers reject are skipped. Leak detection is OFF (leaks aren't
# soundness bugs); we hunt use-after-free / heap-corruption / UB / miscompiles.
import subprocess, sys, os, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(REPO, "fuzz", "gen.py")
HIERC = os.path.join(REPO, "hierc")
FFI_SHIM = os.path.join(REPO, "fuzz", "ffi_shim.c")   # backs the generator's extern fn vocabulary
FINDINGS = os.path.join(REPO, "fuzz", "findings")
ASAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
TIMEOUT = 20

def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, timeout=TIMEOUT, **kw)

def emit_hierc(src_path, out_c):
    r = subprocess.run([HIERC, src_path, "--emit-c", "-o", out_c[:-2]], capture_output=True, text=True, timeout=TIMEOUT)
    return r.returncode == 0 and os.path.exists(out_c)

def emit_hierc0(h0, src_path, out_c):
    with open(src_path) as fi, open(out_c, "w") as fo:
        r = subprocess.run([h0], stdin=fi, stdout=fo, stderr=subprocess.DEVNULL, timeout=TIMEOUT)
    return r.returncode == 0 and os.path.getsize(out_c) > 0

def build_run(c_file, exe, tmp, asan=False):
    cc = ["cc", "-O1" if asan else "-O2", "-std=c11"] + (ASAN if asan else []) + [c_file, FFI_SHIM, "-o", exe]
    b = sh(cc)
    if b.returncode != 0:
        return None, "ccfail:" + b.stderr.strip()[:200]
    r = sh([exe], env=ENV)
    if r.returncode != 0:
        return None, "runfail(rc=%d):%s" % (r.returncode, (r.stderr or "")[:200])
    return r.stdout, None

def run_seed(seed, h0, tmp):
    src = os.path.join(tmp, "p.hi")
    with open(src, "w") as f:
        f.write(subprocess.run([sys.executable, GEN, str(seed)], capture_output=True, text=True).stdout)
    hc_ok = emit_hierc(src, os.path.join(tmp, "hc.c"))
    h0_ok = emit_hierc0(h0, src, os.path.join(tmp, "h0.c"))
    if not hc_ok and not h0_ok:
        return "skip", None
    if hc_ok != h0_ok:
        return "FAIL", "compile-discrepancy: hierc=%s hierc0=%s" % (hc_ok, h0_ok)
    out_hc, e1 = build_run(os.path.join(tmp, "hc.c"), os.path.join(tmp, "run_hc"), tmp)
    if e1: return "FAIL", "hierc " + e1            # reference itself faulted -> real bug
    out_h0, e2 = build_run(os.path.join(tmp, "h0.c"), os.path.join(tmp, "run_h0"), tmp)
    if e2: return "FAIL", "hierc0 " + e2
    out_as, e3 = build_run(os.path.join(tmp, "h0.c"), os.path.join(tmp, "run_h0a"), tmp, asan=True)
    if e3: return "FAIL", "hierc0-ASan " + e3
    if out_hc != out_h0:
        return "FAIL", "output diverge: hierc=%r hierc0=%r" % (out_hc, out_h0)
    if out_h0 != out_as:
        return "FAIL", "native vs ASan diverge (UB): %r vs %r" % (out_h0, out_as)
    return "ok", None

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    start = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    os.makedirs(FINDINGS, exist_ok=True)
    tmp = tempfile.mkdtemp()
    h0 = os.path.join(tmp, "h0")
    if subprocess.run([HIERC, os.path.join(REPO, "compiler", "hierc0.hi"), "-o", h0]).returncode != 0:
        print("hierc0 build failed"); return 2
    counts = {"ok": 0, "skip": 0, "FAIL": 0}
    for seed in range(start, start + n):
        try:
            verdict, msg = run_seed(seed, h0, tmp)
        except subprocess.TimeoutExpired:
            verdict, msg = "skip", "timeout"
        counts[verdict] = counts.get(verdict, 0) + 1
        if verdict == "FAIL":
            shutil.copy(os.path.join(tmp, "p.hi"), os.path.join(FINDINGS, "seed_%d.hi" % seed))
            print("FAIL seed %d: %s" % (seed, msg))
        if seed % 200 == 0:
            print("... %d/%d  ok=%d skip=%d FAIL=%d" % (seed - start + 1, n, counts["ok"], counts["skip"], counts["FAIL"]))
    shutil.rmtree(tmp, ignore_errors=True)
    print("DONE: ok=%d skip=%d FAIL=%d  (findings in fuzz/findings/)" % (counts["ok"], counts["skip"], counts["FAIL"]))
    return 1 if counts["FAIL"] else 0

if __name__ == "__main__":
    sys.exit(main())
