set -eu

cd "$(dirname "$0")/.."
T=$(mktemp -d)
srv=""
echosrv=""
cleanup() {
    if [ -n "$echosrv" ]; then kill -TERM "$echosrv" 2>/dev/null || true; fi
    if [ -n "$srv" ]; then
        kill -TERM "$srv" 2>/dev/null || true
        n=0
        while [ "$n" -lt 40 ] && kill -0 "$srv" 2>/dev/null; do n=$((n + 1)); sleep 0.05; done
        kill -KILL "$srv" 2>/dev/null || true
    fi
    rm -rf "$T"
}
trap cleanup EXIT INT TERM

command -v openssl >/dev/null 2>&1 || { echo "http-verify: SKIPPED (no openssl cli)"; exit 0; }
pkg-config --exists libcurl 2>/dev/null || { echo "http-verify: SKIPPED (no libcurl to build against)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

# --- a CA and a leaf for "localhost", neither of them trusted by this box ----
openssl req -x509 -newkey rsa:2048 -keyout "$T/ca.key" -out "$T/ca.pem" -days 2 -nodes \
    -subj "/CN=tycho-http-test-ca" -addext "basicConstraints=critical,CA:TRUE" >/dev/null 2>&1
openssl req -newkey rsa:2048 -keyout "$T/srv.key" -out "$T/srv.csr" -nodes \
    -subj "/CN=localhost" >/dev/null 2>&1
printf 'subjectAltName=DNS:localhost\n' > "$T/ext"
openssl x509 -req -in "$T/srv.csr" -CA "$T/ca.pem" -CAkey "$T/ca.key" -CAcreateserial \
    -out "$T/srv.pem" -days 2 -extfile "$T/ext" >/dev/null 2>&1
[ -s "$T/srv.pem" ] || { echo "http-verify: SKIPPED (could not mint a test certificate)"; exit 0; }

# CAPATH wants a hashed directory, which is what makes [2b] a different code path
# from [2] rather than the same option spelled twice.
mkdir -p "$T/cadir"
cp "$T/ca.pem" "$T/cadir/ca.pem"
(openssl rehash "$T/cadir" >/dev/null 2>&1 || c_rehash "$T/cadir" >/dev/null 2>&1) || true
capath_ok=0
ls "$T/cadir"/*.0 >/dev/null 2>&1 && capath_ok=1

# --- the server, on a port the kernel chooses. -www makes it answer HTTP. -----
port=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
openssl s_server -www -quiet -accept "$port" -naccept 40 -cert "$T/srv.pem" -key "$T/srv.key" \
    >/dev/null 2>&1 &
srv=$!
# Readiness is a real TCP connect, not a sleep.
ready=0; i=0
while [ "$i" -lt 100 ]; do
    if python3 -c "
import socket,sys
s=socket.socket(); s.settimeout(0.2)
sys.exit(0 if s.connect_ex(('127.0.0.1',$port))==0 else 1)" 2>/dev/null; then ready=1; break; fi
    i=$((i + 1))
done
[ "$ready" -eq 1 ] || { echo "http-verify: SKIPPED (the test server never came up)"; exit 0; }

# --- the probe. Its own directory: siblings share a package. -----------------
mkdir -p "$T/p"
cat > "$T/p/main.ty" <<'EOF'
package main
import "core:http"

# 0 means the request never completed -- which is what a refused certificate
# looks like from here. Any other number means bytes came back.
fn main():
    println(str(http.get_status(args()[1])))
EOF
./tychoc "$T/p/main.ty" -o "$T/probe" >"$T/build.log" 2>&1 || {
    echo "http-verify: FAILED (the probe does not build)"; tail -4 "$T/build.log"; exit 1; }

fail=0
say() { printf '  %-48s status %s\n' "$1" "$2"; }

# [1] untrusted CA -> must FAIL
r1=$(env -u SSL_CERT_FILE -u SSL_CERT_DIR "$T/probe" "https://localhost:$port/" 2>/dev/null || true)
say "[1] self-signed chain, not trusted" "$r1"
[ "$r1" = 0 ] || { echo "  LEAK: an untrusted certificate was ACCEPTED."; fail=1; }

# [2] same server, CA trusted, name matches -> must SUCCEED
r2=$(env -u SSL_CERT_DIR SSL_CERT_FILE="$T/ca.pem" "$T/probe" "https://localhost:$port/" 2>/dev/null || true)
say "[2] same server, SSL_CERT_FILE, name matches" "$r2"
if [ "$r2" != 200 ]; then
    echo "  CONTROL DEAD: the server refuses even a trusted, name-matching client,"
    echo "                so [1] and [3] would pass with no verification at all."
    fail=1
fi

# [2b] the CAPATH path -- a second option, so a second leg
if [ "$capath_ok" -eq 1 ]; then
    r2b=$(env -u SSL_CERT_FILE SSL_CERT_DIR="$T/cadir" "$T/probe" "https://localhost:$port/" 2>/dev/null || true)
    say "[2b] same server, SSL_CERT_DIR (CAPATH)" "$r2b"
    [ "$r2b" = 200 ] || { echo "  SSL_CERT_DIR is documented and does not work."; fail=1; }
else
    echo "  [2b] SKIPPED (no openssl rehash/c_rehash, so no hashed CA dir)"
fi

# [3] same server, CA trusted, name does NOT match -> must FAIL
r3=$(env -u SSL_CERT_DIR SSL_CERT_FILE="$T/ca.pem" "$T/probe" "https://127.0.0.1:$port/" 2>/dev/null || true)
say "[3] same server, CA trusted, name differs" "$r3"
[ "$r3" = 0 ] || { echo "  LEAK: the hostname was NOT checked -- a valid cert for another name was accepted."; fail=1; }

# --- [4] the SCHEME fence, and [4b] the interior-NUL guard --------------------
# Both need a request that really FETCHES as their positive control, and this is
# the only lane in the tree that has one. corelib/test/http used file:// for
# that, which stopped working the day file:// started being refused -- so the
# NUL leg lives here now, where [4ctl] is a live 200 rather than a local read.
mkdir -p "$T/p2"
cat > "$T/p2/main.ty" <<'EOF'
package main
import "core:http"

# Body LENGTH, not status: file:// carries no HTTP status, so a status-only
# probe answers 0 for a refused scheme and for a successful file read alike.
fn main():
    u := args()[1]
    if len(args()) > 2:
        u = u + to_str(to_bytes([0])) + "/ignored"
    println(str(len(http.get_body(u))))
EOF
./tychoc "$T/p2/main.ty" -o "$T/probe2" >"$T/build2.log" 2>&1 || {
    echo "http-verify: FAILED (the scheme probe does not build)"; tail -4 "$T/build2.log"; exit 1; }
printf 'eighteen bytes ok\n' > "$T/local.txt"

n4ctl=$(env -u SSL_CERT_DIR SSL_CERT_FILE="$T/ca.pem" "$T/probe2" "https://localhost:$port/" 2>/dev/null || true)
say "[4ctl] live https fetch, bytes returned" "$n4ctl"
if [ "${n4ctl:-0}" -le 0 ] 2>/dev/null; then
    echo "  CONTROL DEAD: nothing fetched at all, so [4] and [4b] would pass on a"
    echo "                probe that never reached curl."
    fail=1
fi

n4=$(env -u SSL_CERT_DIR SSL_CERT_FILE="$T/ca.pem" "$T/probe2" "file://$T/local.txt" 2>/dev/null || true)
say "[4] file:// URL, 18 bytes on disk" "$n4"
[ "$n4" = 0 ] || { echo "  LEAK: file:// was fetched -- CURLOPT_PROTOCOLS is not fencing the first hop."; fail=1; }

n4b=$(env -u SSL_CERT_DIR SSL_CERT_FILE="$T/ca.pem" "$T/probe2" "https://localhost:$port/" nul 2>/dev/null || true)
say "[4b] the SAME live URL with an interior NUL" "$n4b"
[ "$n4b" = 0 ] || { echo "  LEAK: a URL carrying an interior NUL was fetched (truncated, so a DIFFERENT URL)."; fail=1; }

# --- [5] the POST body is sent by LENGTH, not as a C string -------------------
# The body crosses the FFI as (ptr, len) and curl is told the size, so a body
# with an interior NUL is sent whole. Told nothing, curl calls strlen() on it:
# a 7-byte body with a NUL at offset 2 went out as 2 bytes under a
# Content-Length of 2, and every layer downstream agreed with the truncation.
# The SERVER counts the bytes, not the client: a client-side length would print
# the same number whatever curl actually put on the wire.
cat > "$T/echo.py" <<'EOF'
import http.server, socketserver
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        b = self.rfile.read(n)
        m = str(len(b)).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(m))); self.end_headers()
        self.wfile.write(m)
    def log_message(self, *a): pass
s = socketserver.TCPServer(("127.0.0.1", 0), H)
print(s.server_address[1], flush=True)
s.serve_forever()
EOF
mkfifo "$T/echo.port"
python3 "$T/echo.py" > "$T/echo.port" 2>/dev/null &
echosrv=$!
eport=$(head -1 "$T/echo.port")
mkdir -p "$T/p3"
cat > "$T/p3/main.ty" <<'EOF'
package main
import "core:http"

fn main():
    body := to_str(to_bytes([104, 105, 0, 116, 104, 101, 114]))   # 7 bytes, NUL at offset 2
    if len(args()) > 2:
        body = "hithere"                                          # the same 7, no NUL
    println(http.body(http.post(args()[1], body, "application/octet-stream")))
EOF
./tychoc "$T/p3/main.ty" -o "$T/probe3" >"$T/build3.log" 2>&1 || {
    echo "http-verify: FAILED (the POST probe does not build)"; tail -4 "$T/build3.log"; exit 1; }

n5b=$("$T/probe3" "http://127.0.0.1:$eport/" plain 2>/dev/null || true)
say "[5b] control: 7-byte body, no NUL, bytes received" "$n5b"
[ "$n5b" = 7 ] || { echo "  CONTROL DEAD: a clean 7-byte POST did not arrive as 7, so [5] measures nothing."; fail=1; }

n5=$("$T/probe3" "http://127.0.0.1:$eport/" 2>/dev/null || true)
say "[5] the SAME 7 bytes with a NUL at offset 2" "$n5"
[ "$n5" = 7 ] || { echo "  TRUNCATED: the body was cut at the NUL -- curl was not told the length."; fail=1; }

# --- [C] the control: [1] must be refusing because verification runs ----------
# Built against a COPY, so the tree is never in the unverified state. Both
# options go off together: VERIFYPEER alone still leaves the hostname check
# refusing this chain, and the control would look like a working [1].
cp -R corelib "$T/corelib-nv"
sed -i 's|curl_easy_setopt(c, CURLOPT_USERAGENT|curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);\n    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);\n    curl_easy_setopt(c, CURLOPT_USERAGENT|' \
    "$T/corelib-nv/http/http_shim.c"
grep -q SSL_VERIFYPEER "$T/corelib-nv/http/http_shim.c" || {
    echo "http-verify: FAILED (the control patch did not apply -- it would score nothing)"; exit 1; }
mkdir -p "$T/pc"
cp "$T/p/main.ty" "$T/pc/main.ty"
TYCHO_CORELIB="$T/corelib-nv" ./tychoc "$T/pc/main.ty" -o "$T/probe-nv" >"$T/build-nv.log" 2>&1 || {
    echo "http-verify: FAILED (the control probe does not build)"; tail -4 "$T/build-nv.log"; exit 1; }
rc=$(env -u SSL_CERT_FILE -u SSL_CERT_DIR "$T/probe-nv" "https://localhost:$port/" 2>/dev/null || true)
say "[C] control: verification OFF, same untrusted chain" "$rc"
if [ "$rc" = 0 ]; then
    echo "  CONTROL DEAD: the request fails even with verification turned off, so"
    echo "                [1] and [3] are not measuring verification at all."
    fail=1
fi

[ "$fail" -eq 0 ] || { echo "http-verify: FAIL"; exit 1; }
echo "http-verify: green (an untrusted certificate is refused, the same server is accepted once its CA is trusted through either SSL_CERT_FILE or SSL_CERT_DIR and reached by the name in the cert, refused again under a name the cert does not carry, and accepted by a control with verification off -- so the refusals are verification, not a dead connection; file:// and an interior-NUL URL are both refused while a live fetch through the same probe still returns bytes; and a POST body carrying an interior NUL arrives whole)"
