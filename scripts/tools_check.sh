#!/bin/sh
# Regression guard for the daily-driver tooling (tychofmt + tycho-lsp + tycho).
# Run by `make tools-check` and as a step in `make ci`.
#
#   FORMATTER  (1) idempotent: fmt(fmt(x)) == fmt(x) on every tracked .ty;
#              (2) semantics-preserving: `tychoc --emit-c` byte-identical before vs
#                  after formatting, compiled from the SAME path so the filename
#                  can't skew the diff (checked on the files that compile).
#   LSP        scripted JSON-RPC smoke: initialize replies; a clean buffer
#              publishes [] diagnostics; a broken buffer publishes a diagnostic.
#   DISPATCHER `tycho check/run/build/fmt` end to end: exit statuses, argv
#              forwarding, the binary existing under the name the tool printed,
#              and `fmt -w` refusing a format that would change the program.
#
# A formatter that changed a program, or stopped being idempotent, or an LSP that
# stopped answering, or a dispatcher command that silently does nothing, fails
# the build.
set -u
cd "$(dirname "$0")/.." || exit 2
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) FIND=/usr/bin/find ;; *) FIND=find ;; esac

make -s tychoc tychofmt tycho-lsp || { echo "tools build failed"; exit 1; }
TYCHOC=./tychoc; FMT=./tychofmt; LSP=./tycho-lsp

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
fail=0

echo ">>> formatter: idempotence + semantic preservation"
nfiles=0; ncomp=0; idemfail=0; semfail=0
for f in $("$FIND" . -name '*.ty' -not -path './editors/*' -not -path '*/node_modules/*' -not -path './fuzz/findings/*'); do
    nfiles=$((nfiles + 1))
    "$FMT" "$f" > "$TMP/a.ty" 2>/dev/null
    "$FMT" "$TMP/a.ty" > "$TMP/b.ty" 2>/dev/null
    if ! cmp -s "$TMP/a.ty" "$TMP/b.ty"; then echo "  NOT IDEMPOTENT: $f"; idemfail=$((idemfail + 1)); fail=1; fi
    cp "$f" "$TMP/v.ty"
    if "$TYCHOC" "$TMP/v.ty" --emit-c -o "$TMP/A" >/dev/null 2>&1; then
        ncomp=$((ncomp + 1))
        cp "$TMP/a.ty" "$TMP/v.ty"           # same path -> filename can't skew the C
        "$TYCHOC" "$TMP/v.ty" --emit-c -o "$TMP/B" >/dev/null 2>&1
        if ! cmp -s "$TMP/A.c" "$TMP/B.c"; then echo "  SEMANTIC DRIFT: $f"; semfail=$((semfail + 1)); fail=1; fi
    fi
done
echo "    $nfiles files checked  (compilable=$ncomp)  idempotence-fails=$idemfail  semantic-fails=$semfail"

# Spelling, not just stability: `- 1` was idempotent AND semantically identical, so
# the two checks above passed it for as long as it was wrong.
echo ">>> formatter: canonical spellings"
cat > "$TMP/sp.ty" <<'SP'
fn main():
    a := -1
    b := 3 - 1
    c := -a - b
    d := 1 - -2
    e := -(a + b)
    g := [-1, -2]
    h := -3.5
    println(str(a + b + c + d + e + g[0]) + str(h))
SP
"$FMT" "$TMP/sp.ty" > "$TMP/sp.out" 2>/dev/null
if cmp -s "$TMP/sp.ty" "$TMP/sp.out"; then
    echo "    unary minus binds to its operand; binary minus keeps its spaces; \`- -2\` stays two lexemes"
else
    echo "  SPELLING DRIFT:"; diff "$TMP/sp.ty" "$TMP/sp.out" | head -10; fail=1
fi

echo ">>> lsp: scripted JSON-RPC smoke"
python3 - "$LSP" <<'PY' || fail=1
import subprocess, json, os, sys
lsp = sys.argv[1]
def frame(o):
    b = json.dumps(o).encode(); return b"Content-Length: %d\r\n\r\n%b" % (len(b), b)
ok = "fn f(x: int) -> int:\n    y := x + 1\n    return y\n\nfn main():\n    print(str(f(1)))\n"
bad = "fn main():\n    x := \n"
loopy = "fn main():\n    i := 0\n    for i < 3:\n        print(str(i))\n"   # missing increment -> warning
fstr = 'fn main():\n    x := 5\n    print(f"x is {x} now")\n'                # `x` in a hole (rename) + literal `x is` (skip)
def tp(idn, meth, ln, ch):
    return frame({"jsonrpc":"2.0","id":idn,"method":meth,"params":{"textDocument":{"uri":"file:///ok.ty"},"position":{"line":ln,"character":ch}}})
msgs = (frame({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///ok.ty","text":ok}}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///bad.ty","text":bad}}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///loop.ty","text":loopy}}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///fstr.ty","text":fstr}}})
        + tp(2, "textDocument/hover", 1, 4)        # local `y` -> inferred type
        + tp(3, "textDocument/hover", 5, 14)       # `f` call -> resolved signature
        + tp(4, "textDocument/definition", 5, 14)  # -> fn f line
        + frame({"jsonrpc":"2.0","id":5,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///ok.ty"}}})
        + frame({"jsonrpc":"2.0","id":6,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///ok.ty"},"position":{"line":1,"character":4}}})
        + frame({"jsonrpc":"2.0","id":7,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///ok.ty"},"position":{"line":5,"character":14},"context":{"includeDeclaration":True}}})
        + frame({"jsonrpc":"2.0","id":8,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///ok.ty"},"position":{"line":5,"character":14},"newName":"g"}})
        + frame({"jsonrpc":"2.0","id":9,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///ok.ty"},"range":{}}})
        + tp(11, "textDocument/signatureHelp", 5, 16)   # cursor in f(1) -> f's signature, active param 0
        + frame({"jsonrpc":"2.0","id":12,"method":"workspace/symbol","params":{"query":"f"}})   # -> fn f in ok.ty
        + frame({"jsonrpc":"2.0","id":13,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///ok.ty"}}})   # `fn`=keyword, `f`=function
        + frame({"jsonrpc":"2.0","id":10,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///fstr.ty"},"position":{"line":1,"character":4},"newName":"y"}})
        + frame({"jsonrpc":"2.0","method":"exit"}))
p = subprocess.run([lsp], input=msgs, capture_output=True, timeout=30, env=dict(os.environ, TYCHOC="./tychoc"))
out = p.stdout.decode(); i = 0; init = False; diags = {}; hloc = None; hfn = None; defn = None
res = {}
while True:
    k = out.find("Content-Length: ", i)
    if k < 0: break
    j = out.find("\r\n\r\n", k); n = int(out[k+16:j]); body = out[j+4:j+4+n]; i = j+4+n
    o = json.loads(body)
    if o.get("id") == 1 and "result" in o: init = True
    if o.get("id") == 2: hloc = o.get("result")
    if o.get("id") == 3: hfn = o.get("result")
    if o.get("id") == 4: defn = o.get("result")
    if isinstance(o.get("id"), int) and o["id"] >= 5: res[o["id"]] = o.get("result")
    if o.get("method") == "textDocument/publishDiagnostics":
        diags[o["params"]["uri"]] = o["params"]["diagnostics"]
clean = diags.get("file:///ok.ty") == []
flagged = len(diags.get("file:///bad.ty", [])) >= 1
loc_ok = bool(hloc) and "y: int" in json.dumps(hloc)
fn_ok = bool(hfn) and "f(x: int)" in json.dumps(hfn)
def_ok = bool(defn) and defn.get("range", {}).get("start", {}).get("line") == 0
warn_ok = any(d.get("severity") == 2 for d in diags.get("file:///loop.ty", []))
dsym = res.get(5) or []
sym_ok = any(s.get("name") == "f" for s in dsym) and any(s.get("name") == "main" for s in dsym)
clabels = [it["label"] for it in (res.get(6) or {}).get("items", [])]
comp_ok = "f" in clabels and "print" in clabels and "y" in clabels
refs_ok = isinstance(res.get(7), list) and len(res[7]) == 2          # decl + the call
ren_ok = len(((res.get(8) or {}).get("changes", {})).get("file:///ok.ty", [])) == 2
inlay_ok = isinstance(res.get(9), list) and any(":" in h.get("label", "") for h in res[9])
sig = res.get(11) or {}
sig_ok = (bool(sig) and sig.get("activeParameter") == 0
          and "f(x: int)" in (sig.get("signatures", [{}])[0].get("label", ""))
          and [p["label"] for p in sig.get("signatures", [{}])[0].get("parameters", [])] == ["x: int"])
wsym = res.get(12) or []
wsym_ok = any(s.get("name") == "f" and s.get("location", {}).get("uri") == "file:///ok.ty" for s in wsym)
stok = (res.get(13) or {}).get("data", [])
# ok.ty line 0 is "fn f(x: int) -> int:" -> token 0 is `fn` (keyword=0), token 1 is `f` before `(` (function=2)
stok_ok = len(stok) % 5 == 0 and len(stok) >= 10 and stok[3] == 0 and stok[8] == 2
fren = ((res.get(10) or {}).get("changes", {})).get("file:///fstr.ty", [])
fcols = sorted((e["range"]["start"]["line"], e["range"]["start"]["character"]) for e in fren)
_hl = fstr.split("\n")[2]                                              # 'print(f"x is {x} now")' line
fstr_ok = len(fren) == 2 and (1, 4) in fcols and (2, _hl.index("{x}") + 1) in fcols \
          and not any(l == 2 and c == _hl.index("x is") for (l, c) in fcols)   # hole renamed, literal `x is` skipped
print("    init=%s  diag(valid->[]=%s invalid->diag=%s loop-warn=%s)  hover(local=%s fn=%s)  def=%s" % (init, clean, flagged, warn_ok, loc_ok, fn_ok, def_ok))
print("    docsym=%s  completion=%s  references=%s  rename=%s  inlay=%s  fstr-rename=%s  sighelp=%s  wsym=%s  semtok=%s" % (sym_ok, comp_ok, refs_ok, ren_ok, inlay_ok, fstr_ok, sig_ok, wsym_ok, stok_ok))
sys.exit(0 if (init and clean and flagged and loc_ok and fn_ok and def_ok and warn_ok and sym_ok and comp_ok and refs_ok and ren_ok and inlay_ok and fstr_ok and sig_ok and wsym_ok and stok_ok) else 1)
PY

echo ">>> loop-warning: tychoc warns on a non-advancing for-loop"
# Guards the loop-progress diagnostic. It is stderr-only, so a golden lane that
# compares emitted C on stdout can't catch a regression that silently disables it
# -- this can. Bad loop must warn; good loop must not. (The tychoc0 half of this
# and the two checks below was removed on 2026-07-26 with the freeze; see
# compiler/tychoc0.ty.)
printf 'fn main():\n    i := 0\n    for i < 3:\n        print(str(i))\n' > "$TMP/badloop.ty"
printf 'fn main():\n    i := 0\n    for i < 3:\n        print(str(i))\n        i = i + 1\n' > "$TMP/goodloop.ty"
"$TYCHOC"      "$TMP/badloop.ty"  --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/e1"; cbw=$(grep -c 'warning:' "$TMP/e1")
"$TYCHOC"      "$TMP/goodloop.ty" --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/e2"; cgw=$(grep -c 'warning:' "$TMP/e2")
echo "    tychoc: bad=$cbw good=$cgw"
{ [ "$cbw" -ge 1 ] && [ "$cgw" -eq 0 ]; } || { echo "  LOOP-WARN FAIL"; fail=1; }

echo ">>> pure-result: tychoc warns on a discarded pure-builtin result"
# Same rationale as the loop-warning guard (stderr-only, fixpoint can't see it).
# A bare `m.get(k,d)` discards the value it returns -> must warn; `m[k]=v` must not.
printf 'fn main():\n    m := []string: int\n    m["a"] = 1\n    m.get("a", 0)\n' > "$TMP/pure.ty"
printf 'fn main():\n    m := []string: int\n    m["a"] = 1\n    print(str("a" in m))\n' > "$TMP/nopure.ty"
"$TYCHOC"      "$TMP/pure.ty"   --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/p1"; cpw=$(grep -c 'warning:' "$TMP/p1")
"$TYCHOC"      "$TMP/nopure.ty" --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/p2"; cpn=$(grep -c 'warning:' "$TMP/p2")
echo "    tychoc: bad=$cpw good=$cpn"
{ [ "$cpw" -ge 1 ] && [ "$cpn" -eq 0 ]; } || { echo "  PURE-RESULT FAIL"; fail=1; }

echo ">>> fall-off-the-end: tychoc warns on a non-void proc that can reach its end without returning"
# Same rationale as the loop-warning guard (stderr-only, fixpoint can't see it).
# A `-> int` proc whose `if` has no else + no trailing return can fall off the
# end (codegen zero-fills) -> must warn; a trailing return must not.
printf 'fn f(n: int) -> int:\n    if n > 0:\n        return 1\n\nfn main():\n    print(str(f(1)))\n' > "$TMP/falloff.ty"
printf 'fn f(n: int) -> int:\n    if n > 0:\n        return 1\n    return 0\n\nfn main():\n    print(str(f(1)))\n' > "$TMP/allret.ty"
"$TYCHOC"      "$TMP/falloff.ty" --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/f1"; cfo=$(grep -c 'not all paths' "$TMP/f1")
"$TYCHOC"      "$TMP/allret.ty"  --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/f2"; cfn=$(grep -c 'not all paths' "$TMP/f2")
echo "    tychoc: bad=$cfo good=$cfn"
{ [ "$cfo" -ge 1 ] && [ "$cfn" -eq 0 ]; } || { echo "  FALL-OFF FAIL"; fail=1; }

echo ">>> line-info: -g emits #line mapping + compiles; default stays clean"
# Guards B1 (tychoc-only feature). Default output must carry NO #line so the
# byte-identical fixpoint/corelib gates are untouched; `-g` must emit #line
# directives naming the .ty source and still build+run.
printf 'fn main():\n    x := 41\n    println(str(x + 1))\n' > "$TMP/dbg.ty"
"$TYCHOC" "$TMP/dbg.ty"    --emit-c -o "$TMP/dbg_off" >/dev/null 2>&1; off=$(grep -c '#line' "$TMP/dbg_off.c")
"$TYCHOC" "$TMP/dbg.ty" -g --emit-c -o "$TMP/dbg_on"  >/dev/null 2>&1; on=$(grep -c '#line' "$TMP/dbg_on.c")
onfile=$(grep -c 'dbg\.ty' "$TMP/dbg_on.c")
"$TYCHOC" "$TMP/dbg.ty" -g -o "$TMP/dbg_bin" >/dev/null 2>&1 && "$TMP/dbg_bin" >"$TMP/dbg_out" 2>&1; ran=$?
got=$(cat "$TMP/dbg_out" 2>/dev/null)
echo "    default #line=$off   -g #line=$on (names src=$([ "$onfile" -ge 1 ] && echo yes || echo no))   run=$got"
{ [ "$off" -eq 0 ] && [ "$on" -ge 1 ] && [ "$onfile" -ge 1 ] && [ "$ran" -eq 0 ] && [ "$got" = "42" ]; } || { echo "  LINE-INFO FAIL"; fail=1; }

echo ">>> xpkg: cross-package completion + hover resolve imported members"
# Guards A2: the LSP resolves `import "core:X"` by running --symbols on the file
# in its real directory (package-aware). Needs a real on-disk file + TYCHO_CORELIB.
mkdir -p "$TMP/xpkg"
printf 'package main\nimport "core:strings"\n\nfn main():\n    s := strings.trim("  hi  ")\n    println(s)\n' > "$TMP/xpkg/main.ty"
python3 - "$LSP" "$TMP/xpkg/main.ty" "$PWD/corelib" <<'PY' || fail=1
import subprocess, json, os, sys
lsp, path, corelib = sys.argv[1], sys.argv[2], sys.argv[3]
def frame(o):
    b = json.dumps(o).encode(); return b"Content-Length: %d\r\n\r\n%b" % (len(b), b)
uri = "file://" + path
text = open(path).read()
line4 = text.split("\n")[4]                          # '    s := strings.trim("  hi  ")'
dot = line4.index("strings.") + len("strings.")
trim = line4.index("trim")
msgs = (frame({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":uri,"text":text}}})
        + frame({"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":uri},"position":{"line":4,"character":dot}}})
        + frame({"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":uri},"position":{"line":4,"character":trim}}})
        + frame({"jsonrpc":"2.0","method":"exit"}))
p = subprocess.run([lsp], input=msgs, capture_output=True, timeout=30, env=dict(os.environ, TYCHOC="./tychoc", TYCHO_CORELIB=corelib))
out = p.stdout.decode(); i = 0; res = {}
while True:
    k = out.find("Content-Length: ", i)
    if k < 0: break
    j = out.find("\r\n\r\n", k); n = int(out[k+16:j]); body = out[j+4:j+4+n]; i = j+4+n
    o = json.loads(body)
    if isinstance(o.get("id"), int): res[o["id"]] = o.get("result")
labels = [it["label"] for it in (res.get(2) or {}).get("items", [])]
comp_ok = "trim" in labels and "lines" in labels     # package members, not the generic list
hover_ok = "strings.trim" in json.dumps(res.get(3) or "")
print("    completion(strings.) trim+lines=%s   hover(strings.trim)=%s" % (comp_ok, hover_ok))
sys.exit(0 if comp_ok and hover_ok else 1)
PY

echo ">>> pkgdiag: package-aware diagnostics (sibling resolution + live buffer)"
# A package file is compiled package-aware: its dir is mirrored with the live
# buffer swapped in, so sibling symbols resolve and the active file's real errors
# surface (a lone temp would scan /tmp and drop them). Needs a real on-disk dir.
mkdir -p "$TMP/pkgdiag"
printf 'package main\n\nfn help(x: int) -> int:\n    return x + 1\n' > "$TMP/pkgdiag/help.ty"
printf 'package main\n\nfn main():\n    println("stub")\n' > "$TMP/pkgdiag/main.ty"
python3 - "$LSP" "$TMP/pkgdiag/main.ty" "$PWD/corelib" <<'PY' || fail=1
import subprocess, json, os, sys
lsp, path, corelib = sys.argv[1], sys.argv[2], sys.argv[3]
def frame(o):
    b = json.dumps(o).encode(); return b"Content-Length: %d\r\n\r\n%b" % (len(b), b)
uri = "file://" + path
def diags_for(text):
    msgs = (frame({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})
            + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":uri,"text":text}}})
            + frame({"jsonrpc":"2.0","method":"exit"}))
    p = subprocess.run([lsp], input=msgs, capture_output=True, timeout=30, env=dict(os.environ, TYCHOC="./tychoc", TYCHO_CORELIB=corelib))
    out = p.stdout.decode(); i = 0; d = None
    while True:
        k = out.find("Content-Length: ", i)
        if k < 0: break
        j = out.find("\r\n\r\n", k); n = int(out[k+16:j]); body = out[j+4:j+4+n]; i = j+4+n
        o = json.loads(body)
        if o.get("method") == "textDocument/publishDiagnostics" and o["params"]["uri"] == uri:
            d = o["params"]["diagnostics"]
    return d
# live buffer uses the sibling help() AND has a real error (undefined zzz)
bad = diags_for("package main\n\nfn main():\n    z := help(41)\n    println(zzz)\n")
# clean buffer, still using the sibling -> no diagnostics
clean = diags_for('package main\n\nfn main():\n    println(str(help(41)))\n')
bad_ok = bad is not None and len(bad) >= 1 and "zzz" in json.dumps(bad)
clean_ok = clean == []
print("    pkg real-err flagged=%s   clean pkg empty=%s" % (bad_ok, clean_ok))
sys.exit(0 if bad_ok and clean_ok else 1)
PY

echo ">>> pkgsnip: a package-build parse error names the RIGHT file + shows its source snippet"
# Regression: g_src was left pointing at the last-lexed file, so a parse error in a
# package printed the correct name:line but a source snippet from the wrong (corelib)
# file, or none at all. The error below is on main.ty:6; assert both name:line and the
# offending source line appear together.
mkdir -p "$TMP/pkgsnip"
printf 'package main\nimport "core:strings"\n\nfn main():\n    println(strings.trim("hi"))\n    q := 1 +\n' > "$TMP/pkgsnip/main.ty"
TYCHO_CORELIB="$PWD/corelib" ./tychoc "$TMP/pkgsnip/main.ty" -o "$TMP/pkgsnip/out" 2>"$TMP/pkgsnip.err"
if grep -q 'pkgsnip/main.ty:6:' "$TMP/pkgsnip.err" && grep -q 'q := 1 +' "$TMP/pkgsnip.err"; then
    echo "    package parse error: right file:line + source snippet"
else
    echo "    package parse error: WRONG file/line or missing snippet"; sed 's/^/      /' "$TMP/pkgsnip.err"; fail=1
fi

echo ">>> pkgresolve: a package-build RESOLVE error names the sibling file it's in"
# Regression: resolve/codegen ran on the merged program with g_srcname resting on
# the last-parsed file, so a semantic error in a non-entry sibling was attributed to
# the entry file. Each proc now carries its source file; the error below is in
# helper.ty, not the entry main.ty.
mkdir -p "$TMP/pkgres"
printf 'package main\nfn helper() -> int:\n    return undefined_here\n' > "$TMP/pkgres/helper.ty"
printf 'package main\nfn main():\n    println(str(helper()))\n' > "$TMP/pkgres/main.ty"
TYCHO_CORELIB="$PWD/corelib" ./tychoc "$TMP/pkgres/main.ty" -o "$TMP/pkgres/out" 2>"$TMP/pkgres.err"
if grep -q 'pkgres/helper.ty:3:' "$TMP/pkgres.err" && grep -q 'return undefined_here' "$TMP/pkgres.err"; then
    echo "    package resolve error: right sibling file + snippet"
else
    echo "    package resolve error: WRONG file or missing snippet"; sed 's/^/      /' "$TMP/pkgres.err"; fail=1
fi

echo ">>> bytes-rehome: a bytes field of a returned struct is deep-copied into the caller's arena"
# Regression: copy_into had no T_BYTES case, so a `bytes` field of a returned/stored
# aggregate fell through to `return val` (no re-home) and dangled in the callee's
# freed scope -- a use-after-free that served garbage from the webserver dogfood.
# The bytes must be re-homed (tycho_str_copy, same as a string). Emission-level so
# it's deterministic (the runtime manifestation is arena-layout dependent).
#
# The compile's exit status is CHECKED. It was not, once: `io.read_bytes` became
# `Result(bytes, io.IoErr)` and the old fixture's bare `len(d)` stopped compiling,
# so for three commits this lane asserted nothing and reported its own rot as
# `grep: .../brh/main.c: No such file or directory`. A stale fixture must fail loudly.
mkdir -p "$TMP/brh"
printf 'package main\nimport "core:io"\nimport "core:result"\nstruct B:\n    data: bytes\nfn mk(p: string) -> B:\n    d := result.unwrap_or(io.read_bytes(p), to_bytes(""))\n    if len(d) == 0:\n        return B(to_bytes(""))\n    return B(d)\nfn main():\n    b := mk("Makefile")\n    println(str(len(b.data)))\n' > "$TMP/brh/main.ty"
# -o is REQUIRED, not decoration: `--emit-c` with no -o writes the C to stdout
# (src/tychoc.c:13560, the loops-cleanup plan), which `>/dev/null` would swallow whole.
# This was the one in-tree caller relying on the old sibling-file default, and it
# reported the loss exactly as the header above predicts a stale fixture would:
# `grep: .../brh/main.c: No such file or directory`.
if ! TYCHO_CORELIB="$PWD/corelib" ./tychoc "$TMP/brh/main.ty" --emit-c -o "$TMP/brh/main" >/dev/null 2>"$TMP/brh.err"; then
    echo "    bytes-rehome FIXTURE STALE: it no longer compiles, so this lane asserts NOTHING"
    sed 's/^/      /' "$TMP/brh.err"; fail=1
elif grep -q 'tycho_str_copy(_parent, h_d)' "$TMP/brh/main.c"; then
    echo "    bytes field re-homed on struct return"
else
    echo "    bytes field NOT re-homed -- copy_into missing T_BYTES (dangling UAF!)"; fail=1
fi

echo ">>> dispatcher: tools/tycho.ty -- check / run / build / fmt"
# The dispatcher had no lane of its own: `make debug-check` leg 6 drives `tycho
# debug` and nothing drove run/build/check/fmt, which is how five commands could
# be broken on Windows at once (cmd.exe has no /dev/null, no rm/cp/mv/diff, and
# will not start a program whose name carries no .exe) without a red anywhere.
# Every leg asserts an OBSERVABLE: an exit status, a file that must exist under
# the name the tool printed, or a source file that must be byte-identical.
make -s tycho >/dev/null 2>&1 || { echo "    dispatcher build FAILED"; fail=1; }
bin() { if [ -x "$1" ]; then echo "$1"; else echo "$1.exe"; fi; }   # mingw links <name>.exe
TY=$(bin "$PWD/tycho"); CCA=$(bin "$PWD/tychoc"); FMTA=$(bin "$PWD/tychofmt")
D="$TMP/disp"; mkdir -p "$D"
printf 'fn main():\n    println("hi " + str(len(args())))\n' > "$D/prog.ty"
printf 'fn main():\n    x := \n'                             > "$D/bad.ty"
printf 'fn main():\n    exit(3)\n'                           > "$D/nz.ty"
printf 'fn main():\n        println("x")\n'                  > "$D/messy.ty"

# [1] check: accepts a good file, rejects a broken one with a non-zero status
TYCHOC="$CCA" "$TY" check "$D/prog.ty" >"$D/c1.out" 2>&1; rc1=$?
TYCHOC="$CCA" "$TY" check "$D/bad.ty"  >"$D/c2.out" 2>&1; rc2=$?
if [ "$rc1" -eq 0 ] && grep -q '^ok: ' "$D/c1.out" && [ "$rc2" -ne 0 ]; then
    echo "    [1] check: ok on good, non-zero on broken"
else echo "    [1] check WRONG (rc=$rc1/$rc2)"; sed 's/^/      /' "$D/c1.out" "$D/c2.out" | head -6; fail=1; fi

# [2] run: forwards argv, propagates a non-zero exit, and leaves no temp behind
out=$(TYCHOC="$CCA" "$TY" run "$D/prog.ty" one two 2>&1); rc1=$?
TYCHOC="$CCA" "$TY" run "$D/nz.ty" >"$D/r2.out" 2>&1; rc2=$?
litter=$(ls /tmp/.tycho_run_bin* "C:/tmp/.tycho_run_bin"* 2>/dev/null | head -3)
if [ "$rc1" -eq 0 ] && [ "$out" = "hi 3" ] && [ "$rc2" -ne 0 ] && [ -z "$litter" ]; then
    echo "    [2] run: argv forwarded, exit propagated, temp cleaned"
else echo "    [2] run WRONG (rc=$rc1/$rc2 out='$out' litter='$litter')"; sed 's/^/      /' "$D/r2.out" | head -4; fail=1; fi

# [3] build: the binary must exist under the name the tool PRINTED and must run.
# The printed name is the assertion -- on Windows the linker appends .exe, so a
# tool that prints the bare name is naming a file nobody can start.
out=$(TYCHOC="$CCA" "$TY" build "$D/prog.ty" -o "$D/built" 2>&1); rc=$?
named=$(printf '%s\n' "$out" | sed -n 's/^built //p')
if [ "$rc" -eq 0 ] && [ -n "$named" ] && [ -x "$named" ] && [ "$("$named")" = "hi 1" ]; then
    echo "    [3] build: printed name exists and runs ($(basename "$named"))"
else echo "    [3] build WRONG (rc=$rc named='$named')"; printf '%s\n' "$out" | sed 's/^/      /' | head -4; fail=1; fi

# [4] fmt: preview goes to stdout and does not touch the file; -w rewrites it
# and removes its sibling temp
before=$(cat "$D/messy.ty")
prev=$(TYCHOFMT="$FMTA" "$TY" fmt "$D/messy.ty" 2>&1)
still=$(cat "$D/messy.ty")
TYCHOFMT="$FMTA" TYCHOC="$CCA" "$TY" fmt -w "$D/messy.ty" >"$D/f2.out" 2>&1; rc=$?
after=$(cat "$D/messy.ty")
if [ "$prev" = "$after" ] && [ "$still" = "$before" ] && [ "$rc" -eq 0 ] \
   && [ "$after" != "$before" ] && [ ! -f "$D/messy.ty.tychofmt~" ]; then
    echo "    [4] fmt: preview is read-only, -w installed it, no temp left"
else echo "    [4] fmt WRONG (rc=$rc)"; sed 's/^/      /' "$D/f2.out" | head -4; fail=1; fi

# [5] fmt -w REFUSES when the formatter would change the program, and leaves the
# source byte-identical. Driven by a FAKE formatter that emits a different (but
# compilable) program, because the real one never does -- which is exactly why
# this leg exists: it is the only thing that proves the refusal can fire at all.
printf 'fn main():\n    println("fn main():")\n    println("    println(\\"DIFFERENT\\")")\n' > "$D/fake.ty"
"$CCA" "$D/fake.ty" -o "$D/fakefmt" >"$D/fake.log" 2>&1 || { echo "    [5] fake formatter build failed"; sed 's/^/      /' "$D/fake.log"; fail=1; }
FF=$(bin "$D/fakefmt")
sum_before=$(cat "$D/prog.ty")
TYCHOFMT="$FF" TYCHOC="$CCA" "$TY" fmt -w "$D/prog.ty" >"$D/f5.out" 2>&1; rc=$?
if [ "$rc" -ne 0 ] && grep -q 'REFUSED' "$D/f5.out" && [ "$(cat "$D/prog.ty")" = "$sum_before" ] \
   && [ ! -f "$D/prog.ty.tychofmt~" ]; then
    echo "    [5] fmt -w refuses a semantics-changing format, source untouched"
else echo "    [5] fmt -w DID NOT REFUSE (rc=$rc) -- it may have rewritten the source"; sed 's/^/      /' "$D/f5.out" | head -4; fail=1; fi

if [ "$fail" -ne 0 ]; then echo "tools-check: FAIL"; exit 1; fi
echo "tools-check: ok"
