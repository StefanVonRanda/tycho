set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "tycho-sat: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-sat/expected.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

SAT="$T/tycho-sat"
if ! "$TYCHOC" tools/tycho-sat/main.ty -o "$SAT" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-sat: FAIL"; exit 1
fi

out="$T/all.out"
: > "$out"

# ---------------------------------------------------------------------------
# [1] the pigeonhole instances: PHP(2..9) must all be UNSAT
# ---------------------------------------------------------------------------
for n in 2 3 4 5 6 7 8 9; do
    r=$("$SAT" --php "$n" 2>/dev/null | head -1)
    if [ "$r" != "s UNSATISFIABLE" ]; then
        bad "php$n: got '$r', want 's UNSATISFIABLE'"
    fi
    printf 'php%d: %s\n' "$n" "$r" >> "$out"
done

# ---------------------------------------------------------------------------
# [2] planted instances: SAT with a model the runner verifies itself
# ---------------------------------------------------------------------------
python3 - "$SAT" "$T" >> "$out" <<'PYEOF'
import random, subprocess, sys
# Native-Windows python3 (which is what MSYS2 puts on PATH here) opens stdout
# in TEXT mode, so every \n it prints becomes \r\n and the transcript stops
# matching an LF golden -- a diff where both sides look identical. Force LF.
sys.stdout.reconfigure(newline="\n")
SAT = sys.argv[1]
# The CNF must land in the SAME real directory the shell names below.
# A literal "/tmp/..." does not: python3 here is a NATIVE Windows build
# under MSYS2, so it resolves "/tmp" to C:\\tmp, while the shell handing
# "/tmp/..." to the native solver as an argv gets MSYS2's path conversion
# to C:\\msys64\\tmp -- two different directories for one spelling, so the
# solver read nothing and the transcript lost its "s SATISFIABLE"/"v"/
# "c conflicts=" lines. Taking the run's own $T as an argv makes both
# sides agree (MSYS2 converts it once, identically, for each program).
OUTDIR = sys.argv[2]
def planted(seed, nv, nc, ratio):
    random.seed(seed)
    model = {v: random.choice([True, False]) for v in range(1, nv + 1)}
    cls = []
    for _ in range(nc):
        vs = random.sample(range(1, nv + 1), 3)
        lits = []
        for v in vs:
            if random.random() < ratio:
                lits.append(v if model[v] else -v)
            else:
                lits.append(-v if model[v] else v)
        cls.append(lits)
    return model, cls
def run(path, nv):
    r = subprocess.run([SAT, "solve", path], capture_output=True, text=True).stdout
    lines = r.split("\n")
    if lines[0] != "s SATISFIABLE":
        print("planted nv=%d: FAIL -- %s" % (nv, lines[0])); return False
    model = {}
    for ln in lines:
        if ln.startswith("v "):
            for tok in ln[2:].split():
                if tok != "0":
                    l = int(tok)
                    model[abs(l)] = l > 0
    ncl = 0
    bad = 0
    for ln in open(path):
        if ln.startswith(("p", "c")):
            continue
        ncl += 1
        c = [int(x) for x in ln.split() if x != "0"]
        if not any(model.get(abs(l), False) == (l > 0) for l in c):
            bad += 1
    if bad == 0:
        print("planted nv=%d: SAT, model verifies (0/%d clauses violated)" % (nv, ncl))
        return True
    print("planted nv=%d: FAIL -- model violates %d clauses" % (nv, bad))
    return False
ok = True
for seed, nv, nc in [(7, 50, 200), (11, 100, 400), (13, 150, 600)]:
    model, cls = planted(seed, nv, nc, 0.85)
    p = "%s/plant_%d_%d.cnf" % (OUTDIR, seed, nv)
    with open(p, "w") as f:
        f.write("p cnf %d %d\n" % (nv, len(cls)))
        for c in cls:
            f.write(" ".join(str(l) for l in c) + " 0\n")
    if not run(p, nv):
        ok = False
sys.exit(0 if ok else 1)
PYEOF
rc=$?
if [ "$rc" -ne 0 ]; then
    bad "planted-instance section failed"; sed 's/^/      /' "$T/client.err" 2>/dev/null
fi
"$SAT" solve "$T/plant_11_100.cnf" >> "$out" 2>/dev/null

# ---------------------------------------------------------------------------
# [3] determinism + [4] the learning comparison
# ---------------------------------------------------------------------------
a=$("$SAT" solve "$T/plant_11_100.cnf" 2>/dev/null | md5sum)
b=$("$SAT" solve "$T/plant_11_100.cnf" 2>/dev/null | md5sum)
if [ "$a" = "$b" ]; then
    printf 'determinism: ok\n' >> "$out"
else
    bad "solve is not deterministic"
    printf 'determinism: FAIL\n' >> "$out"
fi
cdl=$("$SAT" solve "$T/plant_11_100.cnf" 2>/dev/null | sed -n 's/^c conflicts=\([0-9]*\).*/\1/p')
dpl=$("$SAT" solve "$T/plant_11_100.cnf" --no-learn 2>/dev/null | sed -n 's/^c conflicts=\([0-9]*\).*/\1/p')
printf 'learning comparison (planted 100 vars): CDCL %s conflicts vs chronological-DPLL %s\n' "$cdl" "$dpl" >> "$out"

# ---------------------------------------------------------------------------
# [5] the golden
# ---------------------------------------------------------------------------
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-sat"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-sat/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-sat: green (PHP(2..9) all UNSAT; 3 planted instances SAT with runner-verified models; deterministic; learning comparison recorded)"
else
    echo "tycho-sat: FAIL"; exit 1
fi
