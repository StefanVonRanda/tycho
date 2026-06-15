#!/bin/sh
# Regression guard for the daily-driver tooling (hierfmt + hier-lsp).
# Run by `make tools-check` and as a step in `make ci`.
#
#   FORMATTER  (1) idempotent: fmt(fmt(x)) == fmt(x) on every tracked .hi;
#              (2) semantics-preserving: `hierc --emit-c` byte-identical before vs
#                  after formatting, compiled from the SAME path so the filename
#                  can't skew the diff (checked on the files that compile).
#   LSP        scripted JSON-RPC smoke: initialize replies; a clean buffer
#              publishes [] diagnostics; a broken buffer publishes a diagnostic.
#
# A formatter that changed a program, or stopped being idempotent, or an LSP that
# stopped answering, fails the build.
set -u
cd "$(dirname "$0")/.." || exit 2

make -s hierc hierfmt hier-lsp || { echo "tools build failed"; exit 1; }
HIERC=./hierc; FMT=./hierfmt; LSP=./hier-lsp

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
fail=0

echo ">>> formatter: idempotence + semantic preservation"
nfiles=0; ncomp=0; idemfail=0; semfail=0
for f in $(find . -name '*.hi' -not -path './editors/*' -not -path '*/node_modules/*' -not -path './fuzz/findings/*'); do
    nfiles=$((nfiles + 1))
    "$FMT" "$f" > "$TMP/a.hi" 2>/dev/null
    "$FMT" "$TMP/a.hi" > "$TMP/b.hi" 2>/dev/null
    if ! cmp -s "$TMP/a.hi" "$TMP/b.hi"; then echo "  NOT IDEMPOTENT: $f"; idemfail=$((idemfail + 1)); fail=1; fi
    cp "$f" "$TMP/v.hi"
    if "$HIERC" "$TMP/v.hi" --emit-c -o "$TMP/A" >/dev/null 2>&1; then
        ncomp=$((ncomp + 1))
        cp "$TMP/a.hi" "$TMP/v.hi"           # same path -> filename can't skew the C
        "$HIERC" "$TMP/v.hi" --emit-c -o "$TMP/B" >/dev/null 2>&1
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
def tp(idn, meth, ln, ch):
    return frame({"jsonrpc":"2.0","id":idn,"method":meth,"params":{"textDocument":{"uri":"file:///ok.hi"},"position":{"line":ln,"character":ch}}})
msgs = (frame({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///ok.hi","text":ok}}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///bad.hi","text":bad}}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///loop.hi","text":loopy}}})
        + tp(2, "textDocument/hover", 1, 4)        # local `y` -> inferred type
        + tp(3, "textDocument/hover", 5, 14)       # `f` call -> resolved signature
        + tp(4, "textDocument/definition", 5, 14)  # -> fn f line
        + frame({"jsonrpc":"2.0","id":5,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///ok.hi"}}})
        + frame({"jsonrpc":"2.0","id":6,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///ok.hi"},"position":{"line":1,"character":4}}})
        + frame({"jsonrpc":"2.0","id":7,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///ok.hi"},"position":{"line":5,"character":14},"context":{"includeDeclaration":True}}})
        + frame({"jsonrpc":"2.0","id":8,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///ok.hi"},"position":{"line":5,"character":14},"newName":"g"}})
        + frame({"jsonrpc":"2.0","id":9,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///ok.hi"},"range":{}}})
        + frame({"jsonrpc":"2.0","method":"exit"}))
p = subprocess.run([lsp], input=msgs, capture_output=True, timeout=30, env=dict(os.environ, HIERC="./hierc"))
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
clean = diags.get("file:///ok.hi") == []
flagged = len(diags.get("file:///bad.hi", [])) >= 1
loc_ok = bool(hloc) and "y: int" in json.dumps(hloc)
fn_ok = bool(hfn) and "f(x: int)" in json.dumps(hfn)
def_ok = bool(defn) and defn.get("range", {}).get("start", {}).get("line") == 0
warn_ok = any(d.get("severity") == 2 for d in diags.get("file:///loop.hi", []))
dsym = res.get(5) or []
sym_ok = any(s.get("name") == "f" for s in dsym) and any(s.get("name") == "main" for s in dsym)
clabels = [it["label"] for it in (res.get(6) or {}).get("items", [])]
comp_ok = "f" in clabels and "print" in clabels and "y" in clabels
refs_ok = isinstance(res.get(7), list) and len(res[7]) == 2          # decl + the call
ren_ok = len(((res.get(8) or {}).get("changes", {})).get("file:///ok.hi", [])) == 2
inlay_ok = isinstance(res.get(9), list) and any(":" in h.get("label", "") for h in res[9])
print("    init=%s  diag(valid->[]=%s invalid->diag=%s loop-warn=%s)  hover(local=%s fn=%s)  def=%s" % (init, clean, flagged, warn_ok, loc_ok, fn_ok, def_ok))
print("    docsym=%s  completion=%s  references=%s  rename=%s  inlay=%s" % (sym_ok, comp_ok, refs_ok, ren_ok, inlay_ok))
sys.exit(0 if (init and clean and flagged and loc_ok and fn_ok and def_ok and warn_ok and sym_ok and comp_ok and refs_ok and ren_ok and inlay_ok) else 1)
PY

echo ">>> loop-warning: hierc + hierc0 both warn on a non-advancing for-loop"
# Guards the loop-progress diagnostic in BOTH compilers. It is stderr-only, so
# `make fixpoint` (which compares emitted C on stdout) can't catch a regression
# that silently disables it -- this can. Bad loop must warn; good loop must not.
"$HIERC" compiler/hierc0.hi -o "$TMP/hierc0" >/dev/null 2>&1 || { echo "  hierc0 build FAILED"; fail=1; }
printf 'fn main():\n    i := 0\n    for i < 3:\n        print(str(i))\n' > "$TMP/badloop.hi"
printf 'fn main():\n    i := 0\n    for i < 3:\n        print(str(i))\n        i = i + 1\n' > "$TMP/goodloop.hi"
"$HIERC"      "$TMP/badloop.hi"  --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/e1"; cbw=$(grep -c 'warning:' "$TMP/e1")
"$HIERC"      "$TMP/goodloop.hi" --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/e2"; cgw=$(grep -c 'warning:' "$TMP/e2")
"$TMP/hierc0" "$TMP/badloop.hi"  --emit-c 1>/dev/null 2>"$TMP/e3"; zbw=$(grep -c 'warning:' "$TMP/e3")
"$TMP/hierc0" "$TMP/goodloop.hi" --emit-c 1>/dev/null 2>"$TMP/e4"; zgw=$(grep -c 'warning:' "$TMP/e4")
echo "    hierc: bad=$cbw good=$cgw   hierc0: bad=$zbw good=$zgw"
{ [ "$cbw" -ge 1 ] && [ "$cgw" -eq 0 ] && [ "$zbw" -ge 1 ] && [ "$zgw" -eq 0 ]; } || { echo "  LOOP-WARN PARITY FAIL"; fail=1; }

echo ">>> pure-result: both compilers warn on a discarded pure-builtin result"
# Same rationale as the loop-warning guard (stderr-only, fixpoint can't see it).
# A bare `map_set(m,k,v)` discards the new map it returns -> must warn; `m[k]=v` must not.
printf 'fn main():\n    m := []string: int\n    map_set(m, "a", 1)\n' > "$TMP/pure.hi"
printf 'fn main():\n    m := []string: int\n    m["a"] = 1\n    print(str(map_has(m, "a")))\n' > "$TMP/nopure.hi"
"$HIERC"      "$TMP/pure.hi"   --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/p1"; cpw=$(grep -c 'warning:' "$TMP/p1")
"$HIERC"      "$TMP/nopure.hi" --emit-c -o "$TMP/x" 1>/dev/null 2>"$TMP/p2"; cpn=$(grep -c 'warning:' "$TMP/p2")
"$TMP/hierc0" "$TMP/pure.hi"   --emit-c 1>/dev/null 2>"$TMP/p3"; zpw=$(grep -c 'warning:' "$TMP/p3")
"$TMP/hierc0" "$TMP/nopure.hi" --emit-c 1>/dev/null 2>"$TMP/p4"; zpn=$(grep -c 'warning:' "$TMP/p4")
echo "    hierc: bad=$cpw good=$cpn   hierc0: bad=$zpw good=$zpn"
{ [ "$cpw" -ge 1 ] && [ "$cpn" -eq 0 ] && [ "$zpw" -ge 1 ] && [ "$zpn" -eq 0 ]; } || { echo "  PURE-RESULT PARITY FAIL"; fail=1; }

if [ "$fail" -ne 0 ]; then echo "tools-check: FAIL"; exit 1; fi
echo "tools-check: ok"
