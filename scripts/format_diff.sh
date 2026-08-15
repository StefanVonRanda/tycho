#!/bin/sh
# Check corelib against things this project did not write: Python's own modules
# where the semantics match exactly, and a stated PROPERTY where they do not.
# (The name says "format" because that is where it started; it now also covers
# calendar arithmetic, sort stability, a float grammar and a path invariant.)
#
# Differential-tested against independent implementations:
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

mkdir -p "$T/csv" "$T/json" "$T/enc" "$T/dt" "$T/so" "$T/pf" "$T/pa" "$T/u8" "$T/rx"
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
cat > "$T/so/main.ty" <<'TY'
package main
import "core:sort"
import "core:io"
import "core:strings"

fn joini(xs: [int]) -> string:
    out := ""
    for i := 0; i < len(xs); i += 1:
        if i > 0:
            out = out + " "
        out = out + str(xs[i])
    return out

# Comma-separated ints per line -> asc | desc | argsort | argsort_desc.
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        xs := []int
        for p in split(line, ","):
            push(xs, strings.parse_int(strings.trim(p)))
        out := joini(sort.asc(xs))
        out = out + "|" + joini(sort.desc(xs))
        out = out + "|" + joini(sort.argsort(xs))
        out = out + "|" + joini(sort.argsort_desc(xs))
        println(out)
TY
cat > "$T/pf/main.ty" <<'TY'
package main
import "core:strings"
import "core:io"

# One candidate per line, "~" for the empty string.
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        t := line
        if t == "~":
            t = ""
        match strings.parse_float(t):
            Ok(f): println("OK " + str(f))
            Err(e): println("ERR")
TY
cat > "$T/pa/main.ty" <<'TY'
package main
import "core:path"
import "core:io"

# One untrusted relative path per line ("~" = empty); print safe_join under a
# fixed base, with "@" standing for the empty (refused) answer.
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        rel := line
        if rel == "~":
            rel = ""
        sj := path.safe_join("/srv/data", rel)
        if sj == "":
            sj = "@"
        println(sj)
TY
cat > "$T/u8/main.ty" <<'TY'
package main
import "core:utf8"
import "core:hex"
import "core:io"

# One hex-encoded byte string per line ("-" = empty); print validity and count.
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        h := line
        if h == "-":
            h = ""
        s := hex.decode(h)
        v := 0
        if utf8.valid(s):
            v = 1
        println(str(v) + " " + str(utf8.count(s)))
TY
cat > "$T/rx/main.ty" <<'TY'
package main
import "core:regex"
import "core:io"
import "core:strings"

# "pattern<TAB>subject" per line -> 1 / 0, or E if the pattern was rejected.
fn main():
    for line in io.read_lines(args()[1]):
        if line == "":
            continue
        pat, subj := strings.split_once(line, "\t")
        re := regex.compile(pat)
        if not regex.ok(re):
            println("E")
            continue
        if regex.is_match(re, subj):
            println("1")
        else:
            println("0")
        regex.release(re)
TY
./tychoc "$T/rx/main.ty" -o "$T/rxp" > "$T/b9.log" 2>&1 || {
    echo "format-diff: FAILED (the regex harness does not build)"; tail -4 "$T/b9.log"; exit 1; }
./tychoc "$T/u8/main.ty" -o "$T/u8p" > "$T/b8.log" 2>&1 || {
    echo "format-diff: FAILED (the utf8 harness does not build)"; tail -4 "$T/b8.log"; exit 1; }
./tychoc "$T/pa/main.ty" -o "$T/pap" > "$T/b7.log" 2>&1 || {
    echo "format-diff: FAILED (the path harness does not build)"; tail -4 "$T/b7.log"; exit 1; }
./tychoc "$T/pf/main.ty" -o "$T/pfp" > "$T/b6.log" 2>&1 || {
    echo "format-diff: FAILED (the parse_float harness does not build)"; tail -4 "$T/b6.log"; exit 1; }
./tychoc "$T/so/main.ty" -o "$T/sop" > "$T/b5.log" 2>&1 || {
    echo "format-diff: FAILED (the sort harness does not build)"; tail -4 "$T/b5.log"; exit 1; }
./tychoc "$T/dt/main.ty" -o "$T/dtp" > "$T/b4.log" 2>&1 || {
    echo "format-diff: FAILED (the datetime harness does not build)"; tail -4 "$T/b4.log"; exit 1; }
./tychoc "$T/enc/main.ty" -o "$T/encp" > "$T/b3.log" 2>&1 || {
    echo "format-diff: FAILED (the codec harness does not build)"; tail -4 "$T/b3.log"; exit 1; }
./tychoc "$T/csv/main.ty"  -o "$T/csvp"  > "$T/b1.log" 2>&1 || {
    echo "format-diff: FAILED (the csv harness does not build)";  tail -4 "$T/b1.log"; exit 1; }
./tychoc "$T/json/main.ty" -o "$T/jsonp" > "$T/b2.log" 2>&1 || {
    echo "format-diff: FAILED (the json harness does not build)"; tail -4 "$T/b2.log"; exit 1; }

python3 - "$T" "$N" <<'PY'
import csv, io, json, pathlib, random, re, struct, subprocess, sys
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

# ---- core:sort --------------------------------------------------------------
# Ordering AND stability. core:sort documents every entry point as stable, and
# Python's sorted is stable too, so the tie order is directly comparable -- which
# matters because an unstable sort is correct on every property except the one
# nobody prints.
scases = [[1], [1, 1], [2, 1], [1, 2, 3], [3, 2, 1], [5, 5, 5, 5], [0, -1, 1],
          [-2147483648, 2147483647, 0]]
scases += [[random.randint(-20, 20) for _ in range(random.randint(1, 12))]
           for _ in range(N)]
p = pathlib.Path(T + "/soin.txt")
p.write_text("\n".join(",".join(map(str, c)) for c in scases) + "\n")
sout = subprocess.run([T + "/sop", str(p)], capture_output=True, text=True).stdout.split("\n")

def stable(c, rev):
    return [str(i) for i in sorted(range(len(c)), key=lambda i: (-c[i] if rev else c[i], i))]

# The control that matters is not "does it sort" -- it is "does the tie order
# discriminate". Score argsort against the ANTI-stable expectation (ties
# reversed): every array that HAS a tie must disagree with it, and no array
# without one can. If those two counts differ, the stability leg is decoration.
ties = sum(1 for c in scases if len(set(c)) < len(c))
anti = sum(1 for c, line in zip(scases, sout)
           if len(line.split("|")) == 4
           and line.split("|")[2].split() != [str(i) for i in
               sorted(range(len(c)), key=lambda i: (c[i], -i))])
if ties == 0 or anti != ties:
    print(f"  SORT CONTROL DEAD: {ties} arrays have ties but {anti} discriminate")
    sys.exit(1)

sbad = {}
for c, line in zip(scases, sout):
    f = line.split("|")
    if len(f) != 4:
        sbad.setdefault("harness", []).append((c, line)); continue
    a, d, ai, ad = [x.split() for x in f]
    for n, g, w in (("asc", a, [str(v) for v in sorted(c)]),
                    ("desc", d, [str(v) for v in sorted(c, reverse=True)]),
                    ("argsort (stable)", ai, stable(c, False)),
                    ("argsort_desc (stable)", ad, stable(c, True))):
        if g != w:
            sbad.setdefault(n, []).append((c, g, w))
for k, v in sbad.items():
    print(f"  sort {k}: {len(v)} mismatches; first {v[0]}")
    fail += len(v)
print(f"  {len(scases)} arrays x 4 orderings against Python's sorted "
      f"({ties} of them with a tie): {sum(len(v) for v in sbad.values())} mismatches")

# ---- strings.parse_float ----------------------------------------------------
# The rare case where the oracle is WRITTEN DOWN: corelib/strings/strings.ty
# states the grammar and lists what it refuses -- leading space, inf/nan, hex
# floats, a comma separator, anything trailing. So acceptance is scored against
# that grammar, and the VALUE against Python's float BIT FOR BIT, because a float
# parser's failure mode is a correctly-shaped answer one ulp out.
GRAM = re.compile(r'^[+-]?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$')
fcands = ["1.5", "-1.5", "+1.5", "0", "0.0", ".5", "-.5", "1.", "1e3", "1E3",
          "1e+3", "1e-3", "5e-324", "1e-320", "1e-400", "1e400", "0.1",
          "3.141592653589793", "1234567890.123456", " 1.5", "1.5 ", "inf",
          "-inf", "nan", "NaN", "Infinity", "0x1p3", "1,5", "1.5x", "x1.5", "",
          "-", "+", "e5", ".", "1e", "1e+", "--1", "1..5", "1.5.6", "00.5", "1_5"]
fcands += [repr(random.uniform(-1e6, 1e6)) for _ in range(N // 2)]
fcands += [f"{random.uniform(-1, 1):.17g}e{random.randint(-320, 308)}" for _ in range(N // 2)]
p = pathlib.Path(T + "/pfin.txt")
p.write_text("\n".join(c or "~" for c in fcands) + "\n", encoding="utf-8")
fout = subprocess.run([T + "/pfp", str(p)], capture_output=True, text=True).stdout.split("\n")

# [c1] the acceptance test must disagree with a deliberately LOOSE grammar
LOOSE = re.compile(r'^\s*[+-]?(?:[0-9]*\.?[0-9]+(?:[eE][+-]?[0-9]+)?|inf|nan).*$')
if sum(1 for c, g in zip(fcands, fout) if g.startswith("OK") != bool(LOOSE.match(c))) == 0:
    print("  PARSE_FLOAT CONTROL DEAD: a loose grammar agreed everywhere"); sys.exit(1)

fbad = []
nacc = 0
for c, g in zip(fcands, fout):
    ing = bool(GRAM.match(c))
    ok = g.startswith("OK")
    if ok and not ing:
        fbad.append(("accepted outside the documented grammar", c, g))
    if not ok and ing:
        try:
            v = float(c)
        except Exception:
            v = None
        # in-grammar refusals are legal ONLY for over/underflow, which the
        # header states: 1e400 and 1e-400 are Err by design.
        if v is not None and v != 0 and abs(v) != float("inf"):
            fbad.append(("refused inside the documented grammar", c, g))
    if ok and ing:
        nacc += 1
        want = float(c)
        got = float(g[3:])
        # [c2] the value test must be bit-exact, not close
        if struct.pack("<d", got) != struct.pack("<d", want):
            fbad.append(("value differs from Python", c, g[3:] + " vs " + repr(want)))
for b in fbad[:4]:
    print(f"  parse_float {b[0]}: {b[1]!r} -> {b[2]}")
print(f"  {len(fcands)} float candidates against the documented grammar and Python "
      f"({nacc} accepted, compared bit for bit): {len(fbad)} mismatches")
fail += len(fbad)

# ---- path.safe_join ---------------------------------------------------------
# Not a differential -- posixpath has different semantics -- but a PROPERTY, and
# the property is the security contract this repo already leans on twice: the web
# server's traversal defence and tycho-ar's zip-slip check both end here.
#
#   an accepted answer must normalise to something under the base
#   a refused answer must be genuinely absolute or genuinely escaping
#
# Both halves matter: only checking the first passes a function that refuses
# everything, and only checking the second passes one that accepts everything.
import posixpath as pp
BASE = "/srv/data"; nb = pp.normpath(BASE)
pseg = ["..", ".", "", "a", "b c", "x.txt", ".hidden", "..a", "a..", "...", "/"]
prels = ["user/report.txt", "../../etc/passwd", "/etc/passwd", "..", "a/../..",
         "a/./b", "a//b", "./", "./.", "a/..", "a/../b", "....//", "..%2f",
         "a/b/../../..", "", "x/../../y", "a/b/c/../../../..", "./../x",
         ".../..", "a/.././../b"]
prels += ["/".join(random.choice(pseg) for _ in range(random.randint(1, 6)))
          for _ in range(N)]
p = pathlib.Path(T + "/pain.txt")
p.write_text("\n".join(r or "~" for r in prels) + "\n")
pout = subprocess.run([T + "/pap", str(p)], capture_output=True, text=True).stdout.split("\n")

# the control: the invariant test must catch an answer that DOES escape, and the
# corpus must actually contain escapes for the refusal half to mean anything.
if pp.normpath("/srv/data/../../etc/passwd").startswith(nb + "/"):
    print("  PATH CONTROL DEAD: an escaping answer was judged inside the base"); sys.exit(1)

pbad = []
pacc = pref = 0
for r, sj in zip(prels, pout):
    if sj == "@":
        pref += 1
        n2 = pp.normpath(pp.join(nb, r)) if r else nb
        if not (r.startswith("/") or not (n2 == nb or n2.startswith(nb + "/"))):
            pbad.append(("refused a path that stays inside", r, n2))
    elif sj:
        pacc += 1
        real = pp.normpath(sj)
        if real != nb and not real.startswith(nb + "/"):
            pbad.append(("ACCEPTED AN ESCAPE", r, sj))
if pref < 10:
    print(f"  PATH CONTROL DEAD: only {pref} of {len(prels)} refused -- the corpus")
    print("                     is not exercising the escape path"); sys.exit(1)
for b in pbad[:4]:
    print(f"  safe_join {b[0]}: {b[1]!r} -> {b[2]}")
print(f"  {len(prels)} untrusted relative paths through path.safe_join "
      f"({pacc} accepted, {pref} refused): {len(pbad)} invariant violations")
fail += len(pbad)

# ---- core:utf8 --------------------------------------------------------------
# A UTF-8 validator that accepts an overlong encoding is a filter bypass: the
# same codepoint reaches the program by a spelling the filter above it did not
# recognise. So the corpus leads with the classic ones and Python's strict
# decoder is the oracle.
u8 = [b"", b"a", "\u00e9".encode(), "\u65e5".encode(), "\U0001f389".encode(), b"\x7f",
      b"\xc0\x80",          # overlong NUL -- the canonical bypass
      b"\xc1\xbf",          # overlong
      b"\xe0\x80\x80",      # overlong 3-byte
      b"\xf0\x80\x80\x80",  # overlong 4-byte
      b"\xed\xa0\x80",      # surrogate D800
      b"\xed\xbf\xbf",      # surrogate DFFF
      b"\xf4\x90\x80\x80",  # above U+10FFFF
      b"\xf5\x80\x80\x80",  # above U+10FFFF
      b"\xfe", b"\xff",     # never valid in UTF-8
      b"\x80", b"\xbf",     # lone continuation
      b"\xc2", b"\xe2\x82", b"\xf0\x9f\x8e",   # truncated 2/3/4-byte
      b"\xc2\xa9", b"\xef\xbb\xbf",           # valid: (c), BOM
      b"\xf4\x8f\xbf\xbf"]                     # U+10FFFF, the maximum
u8 += [bytes(random.randrange(256) for _ in range(random.randint(0, 10)))
       for _ in range(N)]
p = pathlib.Path(T + "/u8in.txt")
p.write_text("\n".join(c.hex() or "-" for c in u8) + "\n")
uout = subprocess.run([T + "/u8p", str(p)], capture_output=True, text=True).stdout.split("\n")

def u8ok(b):
    try:
        b.decode("utf-8"); return True
    except Exception:
        return False

# the control: scored against an INVERTED expectation every case must disagree.
# (Written the other way round the first time -- counting AGREEMENTS with the
# inversion -- which reports 0 and looks like a dead control either way.)
upairs = [(c, l) for c, l in zip(u8, uout) if l]
if sum(1 for c, l in upairs if l.startswith("1") != (not u8ok(c))) != len(upairs):
    print("  UTF8 CONTROL DEAD: the inverted expectation did not disagree everywhere")
    sys.exit(1)
nvalid = sum(1 for c, l in upairs if l.startswith("1"))
if nvalid == 0 or nvalid == len(upairs):
    print(f"  UTF8 CONTROL DEAD: {nvalid} of {len(upairs)} valid -- one side is untested")
    sys.exit(1)

ubad = []
for c, l in upairs:
    f = l.split()
    if len(f) != 2:
        ubad.append(("harness", c.hex(), l)); continue
    v, n = f[0] == "1", int(f[1])
    if v != u8ok(c):
        ubad.append(("validity", c.hex(), f"tycho={v} python={u8ok(c)}"))
    elif v and n != len(c.decode("utf-8")):
        ubad.append(("count", c.hex(), f"tycho={n} python={len(c.decode('utf-8'))}"))
for b in ubad[:4]:
    print(f"  utf8 {b[0]}: {b[1]} {b[2]}")
print(f"  {len(upairs)} byte strings through utf8.valid/count against Python's strict "
      f"decoder ({nvalid} valid, {len(upairs) - nvalid} invalid): {len(ubad)} mismatches")
fail += len(ubad)

# ---- core:regex -------------------------------------------------------------
# ONLY is_match is compared, and the reason is the whole care of this leg: POSIX
# ERE is leftmost-LONGEST while Python's re is leftmost-first, so the two pick
# different SPANS -- `(a|ab)` on "ab" is "ab" in ERE and "a" in Python. The
# LANGUAGE each pattern accepts is the same for standard constructs, so
# membership is comparable and offsets are not. Comparing spans here would have
# produced a pile of "defects" that are a documented dialect difference.
#
# The construct set is chosen to be portable BOTH ways: no \d, \w or \b (not
# ERE), no [[:digit:]] (not Python), no lazy quantifiers (not ERE).
rpats = ["abc", "a.c", "a*", "a+", "a?", "^abc$", "[abc]+", "[^abc]+", "a|b",
         "(ab)+", "x{2,3}", "^$", ".", "[0-9]+", "[a-z]+[0-9]*", "(a|b)c",
         "a(b|c)d", "^[A-Za-z_][A-Za-z0-9_]*$", "col{1,2}or", "(ab|cd)ef",
         "[.]", "a\\.c", "[]]", "[-a]", "(|a)b"]
rsubs = ["", "a", "abc", "aabbcc", "xyz", "AB12", "a.c", "axc", "ab", "abab",
         "x", "xx", "xxx", "color", "colour", "abef", "cdef", "_id9", "9id",
         "-a", "]", "b"]
rcases = [(pp, ss) for pp in rpats for ss in rsubs]
p = pathlib.Path(T + "/rxin.txt")
p.write_text("\n".join(f"{pp}\t{ss}" for pp, ss in rcases) + "\n")
rout = subprocess.run([T + "/rxp", str(p)], capture_output=True, text=True).stdout.split("\n")

rpairs = [(c, g) for c, g in zip(rcases, rout) if g in ("0", "1")]
nm = sum(1 for _, g in rpairs if g == "1")
if nm == 0 or nm == len(rpairs):
    print(f"  REGEX CONTROL DEAD: {nm} of {len(rpairs)} matched -- one side is untested")
    sys.exit(1)
if sum(1 for (pp, ss), g in rpairs if (g == "1") != (re.search(pp, ss) is None)) != len(rpairs):
    print("  REGEX CONTROL DEAD: the inverted expectation did not disagree everywhere")
    sys.exit(1)

rbad = [(pp, ss, g) for (pp, ss), g in rpairs
        if (g == "1") != (re.search(pp, ss) is not None)]
for b in rbad[:4]:
    print(f"  regex /{b[0]}/ vs {b[1]!r}: tycho={b[2]} python={not (b[2] == '1')}")
print(f"  {len(rpairs)} pattern x subject pairs through regex.is_match against Python re "
      f"({nm} match, {len(rpairs) - nm} no-match): {len(rbad)} mismatches")
fail += len(rbad)

sys.exit(1 if fail else 0)
PY
rc=$?
[ "$rc" -eq 0 ] || { echo "format-diff: FAIL"; exit 1; }
echo "format-diff: green (both controls live first, then every csv row-set survives stringify and Python's csv.reader -- including a row of one empty field, a quote, an embedded comma and an embedded newline -- and every json document survives parse+stringify and Python's json, including surrogate pairs, a control character, 1e308 and -0; and sha256, md5, base64, hex and url agree with hashlib, base64 and urllib on every input, at every hash block boundary; and core:datetime agrees with Python's datetime and calendar on 414 timestamps including the 1900 and 2100 non-leap centuries, 2000-02-29 and both sides of 2^31; and core:sort agrees with Python's sorted on value order AND on tie order, with the stability leg proved to discriminate rather than assumed to; and strings.parse_float accepts exactly its documented grammar and returns bit-identical doubles to Python, min subnormal included; and path.safe_join never returns a path that escapes its base and never refuses one that stays inside, over hundreds of hostile relative paths, with the counts printed above rather than repeated here; and core:utf8 agrees with Python's strict decoder on validity and codepoint count, overlong encodings, surrogates and out-of-range sequences all refused; and core:regex agrees with Python's re on MEMBERSHIP over a construct set portable to both, spans deliberately not compared because POSIX is leftmost-longest and Python is leftmost-first)"
