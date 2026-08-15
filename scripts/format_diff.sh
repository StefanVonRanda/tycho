#!/bin/sh
# Differential-test the FORMATS AND CODECS against independent implementations:
# core:csv against Python's `csv`, core:json against Python's `json`, and
# sha256 / md5 / base64 / hex / url against hashlib, base64 and urllib.
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
# A digest is the case where "agrees with its own golden" is least reassuring:
# sha256 has one right answer per input, published everywhere, and this repo's
# archiver stakes file integrity on it. The lengths cover every block boundary a
# padding bug hides behind.
#
#   N=<count> sh scripts/format_diff.sh    generated cases per format (default 400)
set -eu

cd "$(dirname "$0")/.."
N=${N:-400}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

command -v python3 >/dev/null 2>&1 || { echo "format-diff: SKIPPED (no python3)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

mkdir -p "$T/csv" "$T/json" "$T/enc" "$T/dt"
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
cat > "$T/enc/main.ty" <<'TY'
package main
import "core:io"
import "core:sha256"
import "core:md5"
import "core:base64"
import "core:hex"
import "core:url"

# One hex-encoded input per line, "-" for the EMPTY input. Encoding empty as an
# empty line and skipping blanks shifted every answer by one and made all six
# codecs look broken, sha256("") included -- the instrument, not the code.
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        h := line
        if h == "-":
            h = ""
        s := hex.decode(h)
        out := sha256.hex(s)
        out = out + "\t" + md5.hex(s)
        out = out + "\t" + base64.encode(s)
        out = out + "\t" + hex.encode(s)
        out = out + "\t" + url.encode(s)
        out = out + "\t" + hex.encode(base64.decode(base64.encode(s)))
        println(out)
TY
cat > "$T/dt/main.ty" <<'TY'
package main
import "core:datetime"
import "core:io"
import "core:strings"

# One unix timestamp per line; six properties, space separated.
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        dt := datetime.from_unix(strings.parse_int(line))
        wd := datetime.weekday(dt.year, dt.month, dt.day)
        out := datetime.format_iso(dt)
        out = out + " " + datetime.weekday_name(wd)
        out = out + " " + datetime.month_name(dt.month)
        out = out + " " + str(datetime.is_leap(dt.year))
        out = out + " " + str(datetime.days_in_month(dt.year, dt.month))
        out = out + " " + str(datetime.to_unix(dt))
        println(out)
TY
./tychoc "$T/dt/main.ty" -o "$T/dtp" > "$T/b4.log" 2>&1 || {
    echo "format-diff: FAILED (the datetime harness does not build)"; tail -4 "$T/b4.log"; exit 1; }
./tychoc "$T/enc/main.ty" -o "$T/encp" > "$T/b3.log" 2>&1 || {
    echo "format-diff: FAILED (the codec harness does not build)"; tail -4 "$T/b3.log"; exit 1; }
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

# ---- the codecs: sha256, md5, base64, hex, url ------------------------------
# Lengths cover every block boundary that can hide a padding bug: 55/56/57 is
# where sha256 and md5 must spill into a second block, and 63/64/65, 127/128/129
# the same for the block size itself.
import base64 as b64, hashlib, urllib.parse as up
lens = [0, 1, 2, 3, 4, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129, 255, 256, 1000]
blobs = [bytes(random.randrange(256) for _ in range(n)) for n in lens]
blobs += [bytes([0]), bytes([255]), b"abc", b"The quick brown fox jumps over the lazy dog"]
blobs += [bytes(random.randrange(256) for _ in range(random.randint(0, 300))) for _ in range(N)]
p = pathlib.Path(T + "/ein.txt")
p.write_text("\n".join(c.hex() or "-" for c in blobs) + "\n")
eout = subprocess.run([T + "/encp", str(p)], capture_output=True, text=True).stdout.split("\n")

# the control: sha256("") is the most-published digest there is, so if the FIRST
# line does not carry it the harness is misaligned and nothing below means
# anything. That is exactly how this was written the first time -- the empty
# input became an empty line, the reader skipped it, and all six codecs appeared
# broken on 321 of 323 inputs.
E3B = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
if not eout or eout[0].split("\t")[0] != E3B:
    print(f"  CODEC CONTROL DEAD: line 1 should be sha256(\"\") = {E3B[:16]}...,")
    print(f"                      got {eout[0][:70] if eout else '<nothing>'}")
    sys.exit(1)

ebad = {}
for c, line in zip(blobs, eout):
    f = line.split("\t")
    if len(f) != 6:
        ebad.setdefault("harness", []).append((c.hex()[:20], line[:40])); continue
    sha, md, b64s, hx, ue, rt = f
    for name, got, want in (("sha256", sha, hashlib.sha256(c).hexdigest()),
                            ("md5", md, hashlib.md5(c).hexdigest()),
                            ("base64", b64s, b64.b64encode(c).decode()),
                            ("hex", hx, c.hex()),
                            ("base64 round trip", rt, c.hex()),
                            ("url", ue, up.quote(c, safe=""))):
        if got != want:
            ebad.setdefault(name, []).append((c.hex()[:24], got[:34], want[:34]))
for k, v in ebad.items():
    print(f"  {k}: {len(v)} mismatches; first {v[0]}")
    fail += len(v)
print(f"  {len(blobs)} inputs x 6 codecs against hashlib / base64 / urllib: "
      f"{sum(len(v) for v in ebad.values())} mismatches")
# ---- core:datetime ----------------------------------------------------------
# Civil-calendar arithmetic, where the interesting inputs are the ones a naive
# leap rule gets wrong: 1900 and 2100 are NOT leap years, 2000 is.
import calendar, datetime as dtm
ts = [0, 1, -1, -86400, 86399, 86400, 951782400, 1709164800, 4107542400,
      -2208988800, 253402300799, 1234567890, 2147483647, 2147483648]
ts += [random.randint(-3155760000, 4102444800) for _ in range(N)]
p = pathlib.Path(T + "/dtin.txt"); p.write_text("\n".join(str(t) for t in ts) + "\n")
dout = subprocess.run([T + "/dtp", str(p)], capture_output=True, text=True).stdout.split("\n")

def props(t):
    d = dtm.datetime(1970, 1, 1, tzinfo=dtm.timezone.utc) + dtm.timedelta(seconds=t)
    # format_iso is documented as "YYYY-MM-DDTHH:MM:SS" (UTC, NO suffix) at
    # corelib/datetime/datetime.ty -- expecting a trailing Z was the oracle's
    # error, not the code's, and it flagged all 414.
    return [d.strftime("%Y-%m-%dT%H:%M:%S"), d.strftime("%a"), d.strftime("%b"),
            str(calendar.isleap(d.year)).lower(),
            str(calendar.monthrange(d.year, d.month)[1]), str(t)]

# the control: an expectation shifted by one second must be caught
if dout and dout[0].split(" ")[0] == props(ts[0] + 1)[0]:
    print("  DATETIME CONTROL DEAD: a one-second shift compared equal"); sys.exit(1)

names = ["iso", "weekday", "month", "leap", "days_in_month", "to_unix roundtrip"]
dbad = {}
for t, line in zip(ts, dout):
    f = line.split(" ")
    if len(f) != 6:
        dbad.setdefault("harness", []).append((t, line)); continue
    for n, g, w in zip(names, f, props(t)):
        if g != w:
            dbad.setdefault(n, []).append((t, g, w))
for k, v in dbad.items():
    print(f"  datetime {k}: {len(v)} mismatches; first {v[0]}")
    fail += len(v)
print(f"  {len(ts)} timestamps x 6 datetime properties against Python: "
      f"{sum(len(v) for v in dbad.values())} mismatches")

sys.exit(1 if fail else 0)
PY
rc=$?
[ "$rc" -eq 0 ] || { echo "format-diff: FAIL"; exit 1; }
echo "format-diff: green (both controls live first, then every csv row-set survives stringify and Python's csv.reader -- including a row of one empty field, a quote, an embedded comma and an embedded newline -- and every json document survives parse+stringify and Python's json, including surrogate pairs, a control character, 1e308 and -0; and sha256, md5, base64, hex and url agree with hashlib, base64 and urllib on every input, at every hash block boundary; and core:datetime agrees with Python's datetime and calendar on 414 timestamps including the 1900 and 2100 non-leap centuries, 2000-02-29 and both sides of 2^31)"
