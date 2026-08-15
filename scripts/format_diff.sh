#!/bin/sh
# Differential-test the two TEXT FORMATS against independent implementations:
# core:csv against Python's `csv` module, core:json against Python's `json`.
#
# `make corelib` checks both against goldens THIS REPO RECORDED. That proves they
# have not changed; it cannot prove they were ever right. For a format the whole
# point of which is that somebody else can read it back, "agrees with its own
# earlier self" is the wrong property.
#
# It found one: `csv.stringify` states `parse(stringify(rows)) == rows` in its own
# doc comment, and that was false for a row of ONE EMPTY FIELD -- written bare, it
# parses back as a row with NO fields (FRICTION #61). 413 of 414 row-sets were
# fine, which is why a golden never noticed.
#
# BOTH HALVES ARE CONTROLLED, because a differential that reports zero is
# indistinguishable from one that is not comparing: a deliberately wrong
# expectation must be caught, and the correct one must not be.
#
#   N=<count> sh scripts/format_diff.sh    generated cases per format (default 400)
set -eu

cd "$(dirname "$0")/.."
N=${N:-400}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

command -v python3 >/dev/null 2>&1 || { echo "format-diff: SKIPPED (no python3)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

mkdir -p "$T/csv" "$T/json"
cat > "$T/csv/main.ty" <<'TY'
package main
import "core:csv"
import "core:io"
import "core:strings"

# One field per line; `%` ends a row; `~` is the empty field. Newlines and quotes
# arrive escaped so the driver's own format cannot collide with CSV's.
fn main():
    rows := [][string]
    cur := []string
    for line in io.read_lines(args()[1]):
        if line == "%":
            push(rows, cur)
            cur = []string
            continue
        v := line
        if v == "~":
            v = ""
        v = strings.replace(v, "\\n", "\n")
        v = strings.replace(v, "\\q", "\"")
        push(cur, v)
    if len(cur) > 0:
        push(rows, cur)
    print(csv.stringify(rows))
TY
cat > "$T/json/main.ty" <<'TY'
package main
import "core:json"
import "core:io"

# One JSON document per line, parsed and written back.
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        println(json.stringify(json.parse(line)))
TY
./tychoc "$T/csv/main.ty"  -o "$T/csvp"  > "$T/b1.log" 2>&1 || {
    echo "format-diff: FAILED (the csv harness does not build)";  tail -4 "$T/b1.log"; exit 1; }
./tychoc "$T/json/main.ty" -o "$T/jsonp" > "$T/b2.log" 2>&1 || {
    echo "format-diff: FAILED (the json harness does not build)"; tail -4 "$T/b2.log"; exit 1; }

python3 - "$T" "$N" <<'PY'
import csv, io, json, pathlib, random, subprocess, sys
T, N = sys.argv[1], int(sys.argv[2])
random.seed(20260815)                      # deterministic: a mismatch reproduces
fail = 0

# ---- core:csv ---------------------------------------------------------------
def enc(f): return "~" if f == "" else f.replace("\n", "\\n").replace('"', "\\q")

def csv_roundtrip(rows):
    p = pathlib.Path(T + "/cin.txt")
    p.write_text("\n".join("\n".join(enc(f) for f in r) + "\n%" for r in rows) + "\n",
                 encoding="utf-8")
    out = subprocess.run([T + "/csvp", str(p)], capture_output=True, text=True).stdout
    return out, list(csv.reader(io.StringIO(out)))

# the control: a row-set compared against a DELIBERATELY WRONG expectation
_, back = csv_roundtrip([["a", "b"]])
if back == [["a", "XX"]]:
    print("  CSV CONTROL DEAD: a wrong expectation compared equal"); sys.exit(1)
if back != [["a", "b"]]:
    print(f"  CSV CONTROL DEAD: the correct expectation did NOT compare equal ({back})"); sys.exit(1)

edge = [["a", "b"], ['say "hi"', "x"], ["a,b", "c"], ["l1\nl2", "z"], [""],
        ["  pad  ", "t"], ['"', "''"], ["a", 'b,c"d\ne'], ["only"], ["", ""],
        ["ünïcødé", "日本語"], ["=1+1", "+cmd"], ["\ttab", "trail "], ['a""b', "c"]]
alph = 'ab,"\n \t=é'
cases = [[r] for r in edge] + [
    [[ "".join(random.choice(alph) for _ in range(random.randint(0, 8)))
       for _ in range(random.randint(1, 4))] for _ in range(1)]
    for _ in range(N)]
bad = [(r, o, g) for r in cases for o, g in [csv_roundtrip(r)] if g != r]
for r, o, g in bad[:3]:
    print(f"  CSV {r} -> wrote {o!r} -> read back {g}")
print(f"  {len(cases)} row-sets through csv.stringify -> Python csv.reader: {len(bad)} mismatches")
fail += len(bad)

# ---- core:json --------------------------------------------------------------
def mk(d=0):
    t = random.random()
    if d > 2 or t < .3:
        return random.choice([1, -2, 0, 3.5, True, False, None, "", 'a"b', "é\n\t", "日本"])
    if t < .65:
        return [mk(d + 1) for _ in range(random.randint(0, 3))]
    return {f"k{i}": mk(d + 1) for i in range(random.randint(0, 3))}

jedge = ['{"a":1}', '[]', '{}', '"x"', '0', '-0', '1e3', '1.5e-3', 'true', 'null',
         '{"k":"a\\"b"}', '{"k":"a\\\\b"}', '{"k":"tab\\there"}', '{"k":"nl\\nhere"}',
         '{"k":"\\u00e9"}', '{"k":"\\u65e5\\u672c"}', '{"k":"\\ud83c\\udf89"}',
         '{"k":"ctl\\u0001x"}', '{"k":""}', '[1,[2,[3,[4]]]]', '{"a":{"b":{"c":[1,2,3]}}}',
         '{"n":1234567890123}', '{"f":0.1}', '{"f":1e308}', '{"neg":-12.5}',
         '{"esc":"/slash"}', '{"esc":"\\u2028sep"}', '[null,true,false]']
docs = jedge + [json.dumps(mk()) for _ in range(N)]
p = pathlib.Path(T + "/jin.txt"); p.write_text("\n".join(docs) + "\n", encoding="utf-8")
out = subprocess.run([T + "/jsonp", str(p)], capture_output=True, text=True).stdout.split("\n")

# the control, same shape: the first document against a wrong expectation
if json.loads(out[0]) == {"a": 99}:
    print("  JSON CONTROL DEAD: a wrong expectation compared equal"); sys.exit(1)
if json.loads(out[0]) != {"a": 1}:
    print("  JSON CONTROL DEAD: the correct expectation did NOT compare equal"); sys.exit(1)

jbad = []
for src, got in zip(docs, out):
    want = json.loads(src)
    try:
        back = json.loads(got)
    except Exception:
        jbad.append((src, got, "UNPARSEABLE")); continue
    if back != want:
        jbad.append((src, got, back))
for s, g, b in jbad[:3]:
    print(f"  JSON {s[:50]} -> wrote {g[:50]} -> read back {str(b)[:50]}")
print(f"  {len(docs)} documents through json.parse -> json.stringify -> Python json: {len(jbad)} mismatches")
fail += len(jbad)
sys.exit(1 if fail else 0)
PY
rc=$?
[ "$rc" -eq 0 ] || { echo "format-diff: FAIL"; exit 1; }
echo "format-diff: green (both controls live first, then every csv row-set survives stringify and Python's csv.reader -- including a row of one empty field, a quote, an embedded comma and an embedded newline -- and every json document survives parse+stringify and Python's json, including surrogate pairs, a control character, 1e308 and -0)"
