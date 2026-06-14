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
msgs = (frame({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///ok.hi","text":"fn main():\n    print(\"hi\\n\")\n"}}})
        + frame({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///bad.hi","text":"fn main():\n    x := \n"}}})
        + frame({"jsonrpc":"2.0","method":"exit"}))
p = subprocess.run([lsp], input=msgs, capture_output=True, timeout=30, env=dict(os.environ, HIERC="./hierc"))
out = p.stdout.decode(); i = 0; init = False; diags = {}
while True:
    k = out.find("Content-Length: ", i)
    if k < 0: break
    j = out.find("\r\n\r\n", k); n = int(out[k+16:j]); body = out[j+4:j+4+n]; i = j+4+n
    o = json.loads(body)
    if o.get("id") == 1 and "result" in o: init = True
    if o.get("method") == "textDocument/publishDiagnostics":
        diags[o["params"]["uri"]] = o["params"]["diagnostics"]
clean = diags.get("file:///ok.hi") == []
flagged = len(diags.get("file:///bad.hi", [])) >= 1
print("    initialize=%s  valid->[]=%s  invalid->diag=%s" % (init, clean, flagged))
sys.exit(0 if (init and clean and flagged) else 1)
PY

if [ "$fail" -ne 0 ]; then echo "tools-check: FAIL"; exit 1; fi
echo "tools-check: ok"
