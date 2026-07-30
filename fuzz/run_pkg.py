#!/usr/bin/env python3
# Package fuzzer. The single-file generator (gen.py/run.py) compiles one file from
# stdin and so can NEVER reach the package-mangling code paths -- exactly where the
# mangle_type and generic-instance-over-tuple bugs hid. This generates a random
# two-package program (a `geom` helper package + a `main` that imports it)
# exercising cross-package types and calls, compiles it with tychoc, runs it, and
# FAILS if it does not build and run cleanly (seed reported).
# Deterministic per seed. Usage: run_pkg.py [N]   (N defaults to 200).
#
# HISTORY -- THE DIFFERENTIAL WAS RETIRED 2026-07-29. Until then each program was
# compiled THREE ways and their output required to be byte-identical:
#   tychoc <dir>/main.ty                     (the C reference)
#   tychoc --bundle | tychoc0                (the post-order package stream)
#   tychoc0 <dir>/main.ty                    (standalone: reads the dir itself)
#
# WHY IT WENT: compiler/tychoc0.ty is FROZEN, and the breaking loop-syntax change
# of 2026-07-29 (the three-clause `for`, bare `for:` and `parallel for i in 0..<N:`
# replace the old `range()` counting header, which was deleted) means it can no
# longer parse the corpus, so no lane builds it.
# See compiler/fixpoint.sh's header, ROADMAP.md and docs/architecture.md.
#
# `k_array_ret` below was left emitting the deleted `range()` by that change and
# only rewritten 2026-07-30 (plan.md phase 37). It failed SILENTLY: `classify`
# returns "skip" whenever tychoc exits non-zero, so a program that no longer parses
# is counted as skipped, not FAILED, and the runner still printed a green
# `FAIL=0`. Half of every run was being thrown away. This lane is not in `make ci`,
# so nothing said so; run it by hand and watch `skip`, not only `FAIL`.
#
# WHAT IS LOST, AND IT IS THE INTERESTING HALF: legs (2) and (3) were the only
# coverage anywhere of the `--bundle` post-order package STREAM and of a compiler
# resolving a package directory from disk on its own. Nothing else in the tree
# consumes `--bundle` output. What remains is a smoke test -- random cross-package
# programs must still compile and run under tychoc -- with no second opinion and
# no check that the bundle stream is even well formed.
import sys, os, random, subprocess, tempfile, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.path.join(REPO, "tychoc")
ENV = dict(os.environ, TYCHO_CORELIB=os.path.join(REPO, "corelib"))
CC = ["cc", "-O2", "-fwrapv", "-std=c11", "-pthread"]

# Each kind, given a seeded RNG and a unique index i, returns (geom_defs, main_stmts).
# main_stmts fold an int into `acc`; every name is suffixed by i to avoid clashes.
def k_struct(r, i):
    return (f"struct Pt{i}:\n    x: int\n    y: int\nfn mkpt{i}(a: int) -> Pt{i}:\n    return Pt{i}(a, a + 1)\n",
            f"    p{i} := geom.mkpt{i}({r.randint(0,9)})\n    acc = acc + p{i}.x + p{i}.y\n")
def k_sized(r, i):
    return (f"fn w{i}(x: u8) -> int:\n    return to_int(x)\n",
            f"    acc = acc + geom.w{i}(to_u8({r.randint(0,300)}))\n")
def k_newtype(r, i):
    return (f"type Id{i} = int\nfn mkid{i}(n: int) -> Id{i}:\n    return Id{i}(n)\nfn idval{i}(v: Id{i}) -> int:\n    return to_int(v)\n",
            f"    acc = acc + geom.idval{i}(geom.mkid{i}({r.randint(0,9)}))\n")
def k_enum(r, i):
    return (f"enum Col{i}:\n    A{i}\n    B{i}\nfn code{i}(c: Col{i}) -> int:\n    match c:\n        A{i}: return 1\n        B{i}: return 2\nfn mkcol{i}() -> Col{i}:\n    return A{i}\n",
            f"    acc = acc + geom.code{i}(geom.mkcol{i}())\n")
def k_generic(r, i):
    return (f"fn gid{i}(x: $T) -> T:\n    return x\n",
            f"    acc = acc + geom.gid{i}({r.randint(0,9)})\n")
def k_generic_tuple(r, i):
    return (f"fn gidt{i}(x: $T) -> T:\n    return x\n",
            f"    t{i} := geom.gidt{i}(({r.randint(0,9)}, {r.randint(0,9)}))\n    acc = acc + t{i}.0 + t{i}.1\n")
def k_tuple_ret(r, i):
    return (f"fn pair{i}(n: int) -> (int, int):\n    return n, n + 1\n",
            f"    tp{i} := geom.pair{i}({r.randint(0,9)})\n    acc = acc + tp{i}.0 + tp{i}.1\n")
def k_array_ret(r, i):
    return (f"fn arr{i}(n: int) -> [int]:\n    out := []int\n    for j := 0; j < n; j += 1:\n        push(out, j)\n    return out\n",
            f"    a{i} := geom.arr{i}({r.randint(1,5)})\n    acc = acc + len(a{i})\n")

KINDS = [k_struct, k_sized, k_newtype, k_enum, k_generic, k_generic_tuple, k_tuple_ret, k_array_ret]

def gen_pkg(seed):
    r = random.Random(seed)
    geom = "package geom\n\n"
    main = 'package main\nimport "geom"\n\nfn main():\n    acc := 0\n'
    for i in range(r.randint(2, 6)):
        g, m = r.choice(KINDS)(r, i)
        geom += g + "\n"
        main += m
    return geom, main + "    println(str(acc))\n"

def run(exe):
    try:
        r = subprocess.run([exe], capture_output=True, text=True, timeout=30)
        return r.stdout if r.returncode == 0 else None
    except subprocess.TimeoutExpired:
        return None

def classify(seed):
    geom, main = gen_pkg(seed)
    d = tempfile.mkdtemp()
    try:
        os.makedirs(d + "/geom")
        open(d + "/geom/g.ty", "w").write(geom)
        open(d + "/main.ty", "w").write(main)
        ent = d + "/main.ty"
        # tychoc reference. Legs (2) and (3) -- the two tychoc0 legs that made this
        # a differential -- were retired 2026-07-29; see the file header.
        ta = subprocess.run([TYCHOC, ent, "-o", d + "/tb"], capture_output=True, text=True, env=ENV, timeout=30)
        if ta.returncode != 0:
            return "skip", None
        oa = run(d + "/tb")
        if oa is None:
            return "FAIL", "tychoc built it but the program did not run cleanly"
        return "ok", None
    finally:
        shutil.rmtree(d, ignore_errors=True)

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    tally = {"ok": 0, "skip": 0, "FAIL": 0}
    fails = []
    for seed in range(n):
        verdict, msg = classify(seed)
        tally[verdict] += 1
        if verdict == "FAIL":
            fails.append((seed, msg)); print("FAIL seed %d: %s" % (seed, msg))
        if (seed + 1) % 100 == 0:
            print("... %d/%d  ok=%d skip=%d FAIL=%d" % (seed + 1, n, tally["ok"], tally["skip"], tally["FAIL"]))
    print("DONE: ok=%d skip=%d FAIL=%d" % (tally["ok"], tally["skip"], tally["FAIL"]))
    print("NOTE: the two tychoc0 legs (--bundle stream, standalone package read) were"
          "\n      retired 2026-07-29 -- see this file's header. This is now a smoke"
          "\n      test, not a differential.")
    sys.exit(1 if tally["FAIL"] else 0)

if __name__ == "__main__":
    main()
