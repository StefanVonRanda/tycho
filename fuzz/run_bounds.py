#!/usr/bin/env python3
"""Bounds-check elision differential fuzzer (plan-goose-transfers phase 2.2).

Generates random loop-shaped cases that index arrays with affine and
data-dependent indices, including off-by-one bounds, starts and offsets, and
body statements that move or shrink the indexed array mid-loop. Every batch is
built twice -- default elision and TYCHOC_NO_BOUNDS_ELISION=1 -- and each case
runs as its own process in both builds. Stdout, stderr and exit status must be
identical: an out-of-range access must abort in both, never read garbage in
the elided build.

    python3 fuzz/run_bounds.py [N] [SEED]     # default 500 cases, seed 1
"""
import os, random, shutil, subprocess, sys, tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TYCHOC = os.environ.get("TYCHOC") or os.path.join(REPO, "tychoc1")
FINDINGS = os.path.join(REPO, "fuzz", "findings")
BATCH = 25
TIMEOUT = 30
RUN_TIMEOUT = 5

PRELUDE = '''\
fn shrink(a: inout [int]):
    a = []int

fn grow(a: inout [int]):
    push(a, 7)

fn total(a: [int]) -> int:
    t := 0
    for v in a:
        t += v
    return t

'''


def arr(rng, lo, hi, n):
    return "[" + ", ".join(str(rng.randint(lo, hi)) for _ in range(n)) + "]"


def case(rng, k):
    nx = rng.randint(0, 6)
    ny = rng.randint(0, 6)
    xs = arr(rng, -5, 9, nx) if nx else "[]int"
    # ys holds indices into xs: mostly in range, sometimes one past either end
    ys = arr(rng, -1, nx, ny) if ny else "[]int"
    start = rng.choice(["0"] * 6 + ["1", "-1"])
    arrname = rng.choice(["xs"] * 4 + ["ys"])
    bound = rng.choice([f"len({arrname})"] * 6 + [
        f"len({arrname}) + 1", f"len({arrname}) - 1", "len(ys)", "len(xs)", str(rng.randint(0, 6))])
    cmp = rng.choice(["<"] * 7 + ["<="])
    step = rng.choice(["1"] * 6 + ["2"])
    c = rng.choice(["0", "0", "1", "-1"])
    access = rng.choice([
        f"{arrname}[i]", f"{arrname}[i]", f"{arrname}[i]", "xs[i]", "ys[i]",
        f"xs[i + {c}]", f"xs[2 * i + {c}]", f"{arrname}[i - 1]",
        "xs[ys[i]]", f"xs[ys[i] + {c}]", "ys[xs[i] % 3]"])
    mut = rng.choice([""] * 6 + [
        "i += 1", "xs = []int", "ys = [0]", "shrink(&xs)", "shrink(&ys)",
        "grow(&xs)", "s += total(xs)", f"{arrname} = {arrname}[1:]",
        "i := 0", "xs := [1]; s += len(xs)"])
    if mut == "grow(&xs)" and "len(xs)" in bound:
        mut = ""  # grows as fast as i climbs: never terminates
    before = rng.random() < 0.5
    body = []
    if mut and before:
        body.append(mut)
    body.append(f"s += {access}")
    body.append('println(str(i) + " " + str(s))')
    if mut and not before:
        body.append(mut)
        body.append(f"s += {access}")
    lines = [f"fn c{k}():",
             f"    xs := {xs}",
             f"    ys := {ys}",
             "    s := 0",
             f"    for i := {start}; i {cmp} {bound}; i += {step}:"]
    lines += ["        " + b.strip() for st in body for b in st.split(";")]
    lines.append('    println("end " + str(s) + " " + str(len(xs)) + " " + str(len(ys)))')
    return "\n".join(lines) + "\n"


def batch_src(rng, base, n):
    out = [PRELUDE]
    for j in range(n):
        out.append(case(rng, base + j))
        out.append("")
    out.append("fn main():")
    out.append("    a := args()")
    for j in range(n):
        out.append(f'    if a[1] == "{base + j}":')
        out.append(f"        c{base + j}()")
    return "\n".join(out) + "\n"


def build(src_dir, out, no_elide):
    env = dict(os.environ)
    if no_elide:
        env["TYCHOC_NO_BOUNDS_ELISION"] = "1"
    else:
        env.pop("TYCHOC_NO_BOUNDS_ELISION", None)
    return subprocess.run([TYCHOC, os.path.join(src_dir, "p.ty"), "-o", out],
                          capture_output=True, text=True, env=env, timeout=TIMEOUT)


def run(exe, k):
    try:
        r = subprocess.run([exe, str(k)], capture_output=True, text=True, timeout=RUN_TIMEOUT)
        return (r.returncode, r.stdout, r.stderr)
    except subprocess.TimeoutExpired:
        return ("timeout", "", "")


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rng = random.Random(seed)
    tmp = tempfile.mkdtemp(prefix="tycho_bounds_")
    fails = rejects = aborts = 0
    try:
        for base in range(0, n, BATCH):
            cnt = min(BATCH, n - base)
            d = os.path.join(tmp, f"b{base}")  # own dir: sibling .ty files share a package
            os.makedirs(d)
            src = batch_src(rng, base, cnt)
            with open(os.path.join(d, "p.ty"), "w") as f:
                f.write(src)
            ea, eb = os.path.join(d, "elide"), os.path.join(d, "check")
            ra, rb = build(d, ea, False), build(d, eb, True)
            if ra.returncode or rb.returncode:
                # a generated shape the type checker refuses is a generator bug, not a finding
                rejects += 1
                print(f"batch {base}: build failed\n{ra.stderr or rb.stderr}", file=sys.stderr)
                continue
            for k in range(base, base + cnt):
                a, b = run(ea, k), run(eb, k)
                if a[0] != 0:
                    aborts += 1
                if a != b:
                    fails += 1
                    os.makedirs(FINDINGS, exist_ok=True)
                    path = os.path.join(FINDINGS, f"bounds_s{seed}_c{k}.ty")
                    shutil.copy(os.path.join(d, "p.ty"), path)
                    print(f"MISMATCH case {k} ({path}, run with arg {k})\n"
                          f"  elided: {a}\n  checked: {b}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"bounds-fuzz: {n} cases, {aborts} abort in both builds, "
          f"{fails} mismatches, {rejects} batches failed to build")
    sys.exit(1 if fails or rejects else 0)


if __name__ == "__main__":
    main()
