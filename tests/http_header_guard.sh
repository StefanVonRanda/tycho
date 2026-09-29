#!/bin/sh
# A CR/LF Content-Type must be rejected even when a live server is available.
set -eu
cd "$(dirname "$0")/.."
command -v python3 >/dev/null || { echo "http-header-guard: python3 required"; exit 1; }
pkg-config --exists libcurl || { echo "http-header-guard: libcurl development files required"; exit 1; }
tmp=$(mktemp -d)
trap 'kill "${server:-}" 2>/dev/null || true; rm -rf "$tmp"' EXIT HUP INT TERM
cat >"$tmp/server.py" <<'PY'
import http.server
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(n)
        body = b"injected" if "X-Audit" in self.headers else b"clean"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *args): pass
s = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
print(s.server_address[1], flush=True)
s.serve_forever()
PY
mkfifo "$tmp/port"
python3 "$tmp/server.py" >"$tmp/port" 2>/dev/null &
server=$!
port=$(head -1 "$tmp/port")
for compiler in ./tychoc ./tychoc1; do
    "$compiler" tests/fixtures/audit_http_header_guard/main.ty -o "$tmp/probe" >"$tmp/build.log" 2>&1 || {
        echo "http-header-guard: $compiler failed to build"; cat "$tmp/build.log"; exit 1;
    }
    got=$("$tmp/probe" "http://127.0.0.1:$port/")
    [ "$got" = "clean=clean blocked=true bytes_blocked=true" ] || {
        echo "http-header-guard: $compiler got '$got'"; exit 1;
    }
done
echo "http-header-guard: Content-Type CR/LF refused by both POST APIs in both compilers"
