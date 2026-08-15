#!/bin/sh
# Does core:http actually VERIFY the certificate, or does it just say it does?
#
# This is the lane FRICTION #57 said could not be written. `scripts/tls_verify.sh`
# closed the same hole for core:tls; the identical three-way check was built for
# core:http on 2026-08-15 and then REMOVED, because its positive control could not
# be made to pass: nothing could point core:http at a private CA. CURL_CA_BUNDLE is
# read by the curl TOOL, not by libcurl; SSL_CERT_FILE was not honoured, because
# libcurl compiles in a default CAINFO that pre-empts OpenSSL's env-var lookup; and
# the shim exposed no CURLOPT_CAINFO. Without a leg that must SUCCEED, "the
# untrusted server was refused" is indistinguishable from "nothing connected at
# all" -- so `CURLOPT_SSL_VERIFYPEER, 0L` added while debugging would have passed
# every lane in this tree. http_shim.c now honours SSL_CERT_FILE/SSL_CERT_DIR,
# which is what makes the positive control reachable and this lane possible.
#
#   [1] untrusted (self-signed) chain           -> the request FAILS  (status 0)
#   [2] the SAME server, its CA trusted by      -> the request SUCCEEDS (200)
#       SSL_CERT_FILE, reached by the cert's name
#   [2b] the same, via SSL_CERT_DIR (CAPATH)    -> SUCCEEDS -- an implemented but
#       untested second path is decoration
#   [3] the SAME server, CA trusted, reached by -> FAILS: chain validation alone
#       an address the cert does not carry         accepts a valid cert for
#                                                  somebody else
#   [C] a CONTROL built against a COPY of corelib with SSL_VERIFYPEER/VERIFYHOST
#       turned off must ACCEPT what [1] refused. If it does not, [1] is passing
#       for some other reason and this lane is scoring nothing.
#
# No fixed port (the kernel picks), no sleep (readiness is a real connect), no
# network beyond the loopback.
set -eu

cd "$(dirname "$0")/.."
T=$(mktemp -d)
srv=""
# TERM then KILL. A bare `kill` is a REQUEST, and a lane that leaves an
# openssl s_server holding a loopback port is FRICTION #63 -- eleven days of
# orphaned processes. Escalating costs 2s at worst.
cleanup() {
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
echo "http-verify: green (an untrusted certificate is refused, the same server is accepted once its CA is trusted through either SSL_CERT_FILE or SSL_CERT_DIR and reached by the name in the cert, refused again under a name the cert does not carry, and accepted by a control with verification off -- so the refusals are verification, not a dead connection)"
