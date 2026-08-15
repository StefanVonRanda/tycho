#!/bin/sh
# Does core:tls actually VERIFY the certificate, or does it just say it does?
#
# corelib/test/tls/main.ty -- the only tls test there was -- connects to a CLOSED
# LOOPBACK PORT. It proves the FFI round trip and fail-closed handling, and its
# own header says the successful path is verified by hand. What it cannot do is
# tell a REFUSED CERTIFICATE apart from a REFUSED CONNECTION: both give a null
# handle. So `SSL_VERIFY_PEER` becoming `SSL_VERIFY_NONE`, or `SSL_set1_host`
# going away, passed every gate in this tree.
#
# This runs a real TLS server on the loopback with a certificate this box does not
# trust, and asserts three outcomes that MUST DISAGREE with each other:
#
#   [1] untrusted (self-signed) cert          -> connect FAILS
#   [2] the SAME server, its CA trusted,      -> connect SUCCEEDS
#       reached by the name in the cert
#   [3] the SAME server, its CA trusted,      -> connect FAILS
#       reached by an address NOT in the cert
#
# [2] is what makes [1] mean anything: without it, [1] also passes when the server
# never started, which is precisely the failure the old test could not see. [3] is
# what makes [2] mean anything: chain validation alone would accept the wrong
# host, so [3] is the only leg that holds SSL_set1_host.
#
# No fixed port (the kernel picks), no sleep (readiness is a real connect), no
# network beyond the loopback.
set -eu

cd "$(dirname "$0")/.."
T=$(mktemp -d)
srv=""
cleanup() { [ -n "$srv" ] && kill "$srv" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT INT TERM

command -v openssl >/dev/null 2>&1 || { echo "tls-verify: SKIPPED (no openssl cli)"; exit 0; }
pkg-config --exists libssl 2>/dev/null || { echo "tls-verify: SKIPPED (no libssl to build against)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

# --- a CA and a leaf for "localhost", neither of them trusted by this box ----
openssl req -x509 -newkey rsa:2048 -keyout "$T/ca.key" -out "$T/ca.pem" -days 2 -nodes \
    -subj "/CN=tycho-test-ca" -addext "basicConstraints=critical,CA:TRUE" >/dev/null 2>&1
openssl req -newkey rsa:2048 -keyout "$T/srv.key" -out "$T/srv.csr" -nodes \
    -subj "/CN=localhost" >/dev/null 2>&1
printf 'subjectAltName=DNS:localhost\n' > "$T/ext"
openssl x509 -req -in "$T/srv.csr" -CA "$T/ca.pem" -CAkey "$T/ca.key" -CAcreateserial \
    -out "$T/srv.pem" -days 2 -extfile "$T/ext" >/dev/null 2>&1
[ -s "$T/srv.pem" ] || { echo "tls-verify: SKIPPED (could not mint a test certificate)"; exit 0; }

# --- the server, on a port the kernel chooses -------------------------------
port=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
openssl s_server -quiet -accept "$port" -naccept 40 -cert "$T/srv.pem" -key "$T/srv.key" \
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
[ "$ready" -eq 1 ] || { echo "tls-verify: SKIPPED (the test server never came up)"; exit 0; }

# --- the probe --------------------------------------------------------------
mkdir -p "$T/p"
cat > "$T/p/main.ty" <<EOF
package main
import "core:tls"

fn main():
    c := tls.connect(args()[1], $port)
    if is_null(c):
        println("FAIL")
    else:
        println("OK")
        tls.close_conn(c)
EOF
./tychoc "$T/p/main.ty" -o "$T/probe" >"$T/build.log" 2>&1 || {
    echo "tls-verify: FAILED (the probe does not build)"; tail -4 "$T/build.log"; exit 1; }

fail=0
say() { printf '  %-46s %s\n' "$1" "$2"; }

# [1] untrusted CA -> must FAIL
r1=$(env -u SSL_CERT_FILE -u SSL_CERT_DIR "$T/probe" localhost 2>/dev/null || true)
say "[1] self-signed chain, not trusted" "$r1"
[ "$r1" = FAIL ] || { echo "  LEAK: an untrusted certificate was ACCEPTED."; fail=1; }

# [2] same server, CA trusted, name matches -> must SUCCEED
r2=$(SSL_CERT_FILE="$T/ca.pem" "$T/probe" localhost 2>/dev/null || true)
say "[2] same server, CA trusted, name matches" "$r2"
if [ "$r2" != OK ]; then
    echo "  CONTROL DEAD: the server refuses even a trusted, name-matching client,"
    echo "                so [1] and [3] would pass with no verification at all."
    fail=1
fi

# [3] same server, CA trusted, name does NOT match -> must FAIL
r3=$(SSL_CERT_FILE="$T/ca.pem" "$T/probe" 127.0.0.1 2>/dev/null || true)
say "[3] same server, CA trusted, name differs" "$r3"
[ "$r3" = FAIL ] || { echo "  LEAK: the hostname was NOT checked -- a valid cert for another name was accepted."; fail=1; }

# --- core:http is NOT gated here, and the reason is worth writing down --------
# http_shim.c sets no CURLOPT_SSL_VERIFY*, so libcurl's verifying defaults apply
# and it is correct today. The same three-way discrimination was built for it and
# then REMOVED, because the positive control cannot be made to pass: there is no
# way to point core:http at a private CA. CURL_CA_BUNDLE is read by the curl TOOL,
# not by libcurl; SSL_CERT_FILE is not honoured by this build (libcurl 8.21 here,
# measured -- a trusted CA and an untrusted one both give status 0); and the shim
# exposes no CURLOPT_CAINFO. Without a leg that must SUCCEED, "untrusted is
# refused" is indistinguishable from "nothing connected", which is exactly the
# blindness this whole lane exists to remove. Shipping those legs green would
# have been decoration. See docs/internals/FRICTION.md.

[ "$fail" -eq 0 ] || { echo "tls-verify: FAIL"; exit 1; }
echo "tls-verify: green (an untrusted certificate is refused, the same server is accepted once its CA is trusted and reached by the name in the cert, and refused again when reached by a name the cert does not carry -- so the refusals are verification, not a dead connection)"
