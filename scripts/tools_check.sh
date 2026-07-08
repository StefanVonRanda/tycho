#!/bin/sh
# Regression guard for the daily-driver tooling (tychofmt + tycho-lsp).
# Run by `make tools-check` and as a step in `make ci`.
#
#   FORMATTER  (1) idempotent: fmt(fmt(x)) == fmt(x) on every tracked .ty;
#              (2) semantics-preserving: `tychoc --emit-c` byte-identical before vs
#                  after formatting, compiled from the SAME path so the filename
#                  can't skew the diff (checked on the files that compile).
#   LSP        scripted JSON-RPC smoke: initialize replies; a clean buffer
#              publishes [] diagnostics; a broken buffer publishes a diagnostic.
#
# A formatter that changed a program, or stopped being idempotent, or an LSP that
# stopped answering, fails the build.
set -u
cd "$(dirname "$0")/.." || exit 2

make -s tychoc tychofmt tycho-lsp || { echo "tools build failed"; exit 1; }
TYCHOC=./tychoc; FMT=./tychofmt; LSP=./tycho-lsp

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
fail=0

echo ">>> formatter: idempotence + semantic preservation"
nfiles=0; ncomp=0; idemfail=0; semfail=0
for f in $(find . -name '*.ty' -not -path './editors/*' -not -path '*/node_modules/*' -not -path './fuzz/findings/*'); do
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
fren = ((res.get(10) or {}).get("changes", {})).get("file:///fstr.ty", [])
fcols = sorted((e["range"]["start"]["line"], e["range"]["start"]["character"]) for e in fren)
_hl = fstr.split("\n")[2]                                              # 'print(f"x is {x} now")' line
fstr_ok = len(fren) == 2 and (1, 4) in fcols and (2, _hl.index("{x}") + 1) in fcols \
          and not any(l == 2 and c == _hl.index("x is") for (l, c) in fcols)   # hole renamed, literal `x is` skipped
print("    init=%s  diag(valid->[]=%s invalid->diag=%s loop-warn=%s)  hover(local=%s fn=%s)  def=%s" % (init, clean, flagged, warn_ok, loc_ok, fn_ok, def_ok))
print("    docsym=%s  completion=%s  references=%s  rename=%s  inlay=%s  fstr-rename=%s" % (sym_ok, comp_ok, refs_ok, ren_ok, inlay_ok, fstr_ok))
sys.exit(0 if (init and clean and flagged and loc_ok and fn_ok and def_ok and warn_ok and sym_ok and comp_ok and refs_ok and ren_ok and inlay_ok and fstr_ok) else 1)
PY

echo ">>> loop-warning: tychoc + tychoc0 both warn on a non-advancing for-loop"
# Guards the loop-progress diagnostic in BOTH compilers. It is stderr-only, so
# `make fixpoint` (which compares emitted C on stdout) can't catch a regression
# that silently disables it -- this can. Bad loop must warn; good loop must not.
"$TYCHOC" compiler/tychoc0.ty -o "$TMP/tychoc0" >/dev/null 2>&1 || { echo "  tychoc0 build FAILED"; fail=1; }
printf 'fn main():\n    i := 0\n    for i < 3:\n        print(str(i))\n' > "$TMP/badloop.ty"
printf 'fn main():\n    i := 0\n    for i < 3:\n        print(str(i))\n        i = i + 1\n' > "$TMP/goodloop.ty"
"$TYCHOC"      "$TMP/badloop.ty"  --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/e1"; cbw=$(grep -c 'warning:' "$TMP/e1")
"$TYCHOC"      "$TMP/goodloop.ty" --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/e2"; cgw=$(grep -c 'warning:' "$TMP/e2")
"$TMP/tychoc0" "$TMP/badloop.ty"  --emit-c 1>/dev/null 2>"$TMP/e3"; zbw=$(grep -c 'warning:' "$TMP/e3")
"$TMP/tychoc0" "$TMP/goodloop.ty" --emit-c 1>/dev/null 2>"$TMP/e4"; zgw=$(grep -c 'warning:' "$TMP/e4")
echo "    tychoc: bad=$cbw good=$cgw   tychoc0: bad=$zbw good=$zgw"
{ [ "$cbw" -ge 1 ] && [ "$cgw" -eq 0 ] && [ "$zbw" -ge 1 ] && [ "$zgw" -eq 0 ]; } || { echo "  LOOP-WARN PARITY FAIL"; fail=1; }

echo ">>> pure-result: both compilers warn on a discarded pure-builtin result"
# Same rationale as the loop-warning guard (stderr-only, fixpoint can't see it).
# A bare `map_get(m,k,d)` discards the value it returns -> must warn; `m[k]=v` must not.
printf 'fn main():\n    m := []string: int\n    m["a"] = 1\n    map_get(m, "a", 0)\n' > "$TMP/pure.ty"
printf 'fn main():\n    m := []string: int\n    m["a"] = 1\n    print(str("a" in m))\n' > "$TMP/nopure.ty"
"$TYCHOC"      "$TMP/pure.ty"   --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/p1"; cpw=$(grep -c 'warning:' "$TMP/p1")
"$TYCHOC"      "$TMP/nopure.ty" --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/p2"; cpn=$(grep -c 'warning:' "$TMP/p2")
"$TMP/tychoc0" "$TMP/pure.ty"   --emit-c 1>/dev/null 2>"$TMP/p3"; zpw=$(grep -c 'warning:' "$TMP/p3")
"$TMP/tychoc0" "$TMP/nopure.ty" --emit-c 1>/dev/null 2>"$TMP/p4"; zpn=$(grep -c 'warning:' "$TMP/p4")
echo "    tychoc: bad=$cpw good=$cpn   tychoc0: bad=$zpw good=$zpn"
{ [ "$cpw" -ge 1 ] && [ "$cpn" -eq 0 ] && [ "$zpw" -ge 1 ] && [ "$zpn" -eq 0 ]; } || { echo "  PURE-RESULT PARITY FAIL"; fail=1; }

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

if [ "$fail" -ne 0 ]; then echo "tools-check: FAIL"; exit 1; fi
echo "tools-check: ok"
