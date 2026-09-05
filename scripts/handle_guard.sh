set -eu

# Handle-guard lane: core:crypto, core:tls, core:http and core:image hand a bare
# `ptr` to Tycho. A `ptr` is NOT affine, so the compiler cannot see a second free
# -- before this guard a double free was a SIGSEGV (rc 139) and a use-after-free
# read freed memory and returned it (rc 0, a garbage length). Both are run-time
# diagnostics now, and no other lane can redden for them: `make corelib` runs
# only correct programs, and a golden recorded from a build with the guard gone
# is byte-identical, because a correct program never trips it.
#
# Three legs per package, from ONE probe binary driven by argv:
#   [ok]  open / use / free           -- must still work, rc 0
#   [df]  open / use / free / free    -- rc != 0, "double free of <handle>"
#   [uaf] open / free / use           -- rc != 0, "<handle> used after free"
# [ok] is not decoration: a guard that fires on everything passes [df] and [uaf]
# and breaks every caller in the tree.
#
# Then two builds against COPIES of corelib, which is what stops the three legs
# above being a report on something else entirely:
#   [ctl] the guard's own condition replaced by `if (1) return;` -- the message
#         must be GONE while the probe still reaches the free
#   [rev] the same copy unpatched -- the message must be BACK, so the difference
#         is the guard and not the copy.

cd "$(dirname "$0")/.."
T=$(mktemp -d)
tlssrv=""
httpsrv=""
cleanup() {
    for pid in $tlssrv $httpsrv; do
        kill -TERM "$pid" 2>/dev/null || true
        n=0
        while [ "$n" -lt 40 ] && kill -0 "$pid" 2>/dev/null; do n=$((n + 1)); sleep 0.05; done
        kill -KILL "$pid" 2>/dev/null || true
    done
    rm -rf "$T"
}
trap cleanup EXIT INT TERM

[ -x ./tychoc ] || make tychoc >/dev/null
command -v python3 >/dev/null 2>&1 || { echo "handle-guard: SKIPPED (no python3)"; exit 0; }

fail=0
legs=0
skipped=""

say_fail() { echo "  FAIL $*"; fail=1; }

free_port() {
    python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()'
}
wait_port() {
    i=0
    while [ "$i" -lt 200 ]; do
        if python3 -c "
import socket,sys
s=socket.socket(); s.settimeout(0.2)
sys.exit(0 if s.connect_ex(('127.0.0.1',$1))==0 else 1)" 2>/dev/null; then return 0; fi
        i=$((i + 1))
    done
    return 1
}

# run <label> <binary> <mode> <want-rc-zero:0|1> <needle-or-> <must-appear-on-stdout>
# want-rc-zero 0 means "must exit 0"; 1 means "must exit non-zero".
run_leg() {
    label=$1; bin=$2; mode=$3; wantzero=$4; needle=$5; stdneedle=$6
    legs=$((legs + 1))
    rc=0
    "$bin" "$mode" >"$T/o" 2>"$T/e" || rc=$?
    if [ "$wantzero" -eq 0 ]; then
        [ "$rc" -eq 0 ] || { say_fail "$label: expected exit 0, got $rc"; sed 's/^/        /' "$T/e"; return; }
    else
        [ "$rc" -ne 0 ] || { say_fail "$label: expected a non-zero exit, got 0"; return; }
    fi
    if [ "$needle" != "-" ]; then
        if ! grep -qF "$needle" "$T/e"; then
            say_fail "$label: stderr does not carry \"$needle\""; sed 's/^/        /' "$T/e"; return
        fi
    elif grep -qE 'used after free|double free of' "$T/e"; then
        say_fail "$label: the guard fired on a legitimate sequence"; sed 's/^/        /' "$T/e"; return
    fi
    if [ -n "$stdneedle" ]; then
        grep -qF "$stdneedle" "$T/o" || { say_fail "$label: stdout does not carry \"$stdneedle\" -- the probe never reached the handle"; return; }
    fi
    echo "  ok   $label"
}

# controls <pkg> <probe-dir> <stdout-needle> <df-needle> [env-prefix...]
# Builds the same probe against a defeated COPY of corelib and against a pristine
# one. The patch is applied by a regex whose match count is ASSERTED: a control
# that silently did not apply reads as confirmation of the guard.
controls() {
    pkg=$1; dir=$2; stdneedle=$3; needle=$4
    cp -R corelib "$T/cl-$pkg-ctl"
    cp -R corelib "$T/cl-$pkg-rev"
    python3 - "$T/cl-$pkg-ctl/$pkg" <<'PY' || { say_fail "$pkg [ctl]: the guard patch did not apply"; return; }
import re, sys, pathlib
d = pathlib.Path(sys.argv[1])
src = next(d.glob("*_shim.c"))
s = src.read_text()
pat = re.compile(r"if \(!(\w+) \|\| \1->magic == \w+_LIVE\) return;")
assert len(pat.findall(s)) == 1, f"{src}: {len(pat.findall(s))} guard conditions, expected 1"
src.write_text(pat.sub("if (1) return;", s))
PY
    legs=$((legs + 1))
    TYCHO_CORELIB="$T/cl-$pkg-ctl" ./tychoc "$dir/main.ty" -o "$T/$pkg-ctl" >"$T/b" 2>&1 || {
        say_fail "$pkg [ctl]: the defeated copy did not build"; sed 's/^/        /' "$T/b"; return; }
    rc=0
    "$T/$pkg-ctl" df >"$T/o" 2>"$T/e" || rc=$?
    if ! grep -qF "$stdneedle" "$T/o"; then
        say_fail "$pkg [ctl]: the control never reached the handle"; return
    fi
    if grep -qF "$needle" "$T/e"; then
        say_fail "$pkg [ctl]: the guard still fired with its condition removed"; return
    fi
    echo "  ok   $pkg [ctl] guard defeated -> no diagnosis (rc $rc)"

    legs=$((legs + 1))
    TYCHO_CORELIB="$T/cl-$pkg-rev" ./tychoc "$dir/main.ty" -o "$T/$pkg-rev" >"$T/b" 2>&1 || {
        say_fail "$pkg [rev]: the pristine copy did not build"; sed 's/^/        /' "$T/b"; return; }
    rc=0
    "$T/$pkg-rev" df >"$T/o" 2>"$T/e" || rc=$?
    if [ "$rc" -eq 0 ] || ! grep -qF "$needle" "$T/e"; then
        say_fail "$pkg [rev]: the revert did not restore the diagnosis (rc $rc)"; return
    fi
    echo "  ok   $pkg [rev] revert restores it"
}

# ============================ core:crypto ============================
if pkg-config --exists libcrypto 2>/dev/null; then
    echo "core:crypto"
    mkdir -p "$T/p-crypto"
    cat > "$T/p-crypto/main.ty" <<'EOF'
package main
import "core:crypto"

fn main():
    mode := args()[1]
    k := crypto.key_random(32)
    println("len " + str(crypto.key_len(k)))
    println("mac " + crypto.hmac_sha256(k, "00"))
    crypto.key_free(k)
    if mode == "df":
        crypto.key_free(k)
    if mode == "uaf":
        println("len " + str(crypto.key_len(k)))
    println("survived")
EOF
    ./tychoc "$T/p-crypto/main.ty" -o "$T/crypto" >"$T/b" 2>&1 || { say_fail "crypto: probe did not build"; sed 's/^/        /' "$T/b"; }
    if [ -x "$T/crypto" ]; then
        run_leg "crypto [ok]"  "$T/crypto" ok  0 -                                        "survived"
        run_leg "crypto [df]"  "$T/crypto" df  1 "tycho: double free of crypto key handle" "len 32"
        run_leg "crypto [uaf]" "$T/crypto" uaf 1 "tycho: crypto key handle used after free" "len 32"
        controls crypto "$T/p-crypto" "len 32" "tycho: double free of crypto key handle"
    fi
else
    skipped="$skipped crypto(missing: libcrypto)"
fi

# ============================ core:image =============================
if pkg-config --exists libpng 2>/dev/null; then
    echo "core:image"
    mkdir -p "$T/p-image"
    # imgx_* are the shim's own externs; core:image frees the handle inside
    # decode(), so this is the only way a caller can hold one across statements.
    cat > "$T/p-image/main.ty" <<'EOF'
package main
import "core:image"

fn main():
    mode := args()[1]
    st := 0
    px := [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 9, 9, 9, 255]
    b := image.encode(image.make(2, 2, to_bytes(px)))
    match b:
        Ok(png):
            h := imgx_decode(png, &st)
            println("w " + str(imgx_width(h)) + " h " + str(imgx_height(h)))
            imgx_free(h)
            if mode == "df":
                imgx_free(h)
            if mode == "uaf":
                println("w " + str(imgx_width(h)))
            println("survived")
        Err(e):
            println("encode failed")
EOF
    ./tychoc "$T/p-image/main.ty" -o "$T/image" >"$T/b" 2>&1 || { say_fail "image: probe did not build"; sed 's/^/        /' "$T/b"; }
    if [ -x "$T/image" ]; then
        run_leg "image [ok]"  "$T/image" ok  0 -                                    "survived"
        run_leg "image [df]"  "$T/image" df  1 "tycho: double free of image handle"  "w 2 h 2"
        run_leg "image [uaf]" "$T/image" uaf 1 "tycho: image handle used after free" "w 2 h 2"
        controls image "$T/p-image" "w 2 h 2" "tycho: double free of image handle"
    fi
else
    skipped="$skipped image(missing: libpng)"
fi

# ============================= core:http =============================
if pkg-config --exists libcurl 2>/dev/null; then
    echo "core:http"
    hport=$(free_port)
    echo hi > "$T/www.txt"
    ( cd "$T" && exec python3 -m http.server "$hport" --bind 127.0.0.1 ) >/dev/null 2>&1 &
    httpsrv=$!
    if wait_port "$hport"; then
        mkdir -p "$T/p-http"
        cat > "$T/p-http/main.ty" <<EOF
package main
import "core:http"

fn main():
    mode := args()[1]
    r := http.get("http://127.0.0.1:$hport/www.txt")
    if is_null(r):
        println("no server")
        return
    println("status " + str(http.status(r)))
    http.release(r)
    if mode == "df":
        http.release(r)
    if mode == "uaf":
        println("status " + str(http.status(r)))
    println("survived")
EOF
        ./tychoc "$T/p-http/main.ty" -o "$T/http" >"$T/b" 2>&1 || { say_fail "http: probe did not build"; sed 's/^/        /' "$T/b"; }
        if [ -x "$T/http" ]; then
            run_leg "http [ok]"  "$T/http" ok  0 -                                            "survived"
            run_leg "http [df]"  "$T/http" df  1 "tycho: double free of http response handle"  "status 200"
            run_leg "http [uaf]" "$T/http" uaf 1 "tycho: http response handle used after free" "status 200"
            controls http "$T/p-http" "status 200" "tycho: double free of http response handle"
        fi
    else
        skipped="$skipped http(the test server never came up)"
    fi
else
    skipped="$skipped http(missing: libcurl)"
fi

# ============================== core:tls =============================
if pkg-config --exists openssl 2>/dev/null && command -v openssl >/dev/null 2>&1; then
    echo "core:tls"
    # The same untrusted CA + localhost leaf tls-verify mints, trusted for this
    # probe through SSL_CERT_FILE: the subject here is the handle, not the chain.
    openssl req -x509 -newkey rsa:2048 -keyout "$T/ca.key" -out "$T/ca.pem" -days 2 -nodes \
        -subj "/CN=tycho-handle-ca" -addext "basicConstraints=critical,CA:TRUE" >/dev/null 2>&1
    openssl req -newkey rsa:2048 -keyout "$T/srv.key" -out "$T/srv.csr" -nodes \
        -subj "/CN=localhost" >/dev/null 2>&1
    printf 'subjectAltName=DNS:localhost\n' > "$T/ext"
    openssl x509 -req -in "$T/srv.csr" -CA "$T/ca.pem" -CAkey "$T/ca.key" -CAcreateserial \
        -out "$T/srv.pem" -days 2 -extfile "$T/ext" >/dev/null 2>&1
    if [ -s "$T/srv.pem" ]; then
        tport=$(free_port)
        openssl s_server -quiet -accept "$tport" -naccept 40 -cert "$T/srv.pem" -key "$T/srv.key" \
            >/dev/null 2>&1 &
        tlssrv=$!
        if wait_port "$tport"; then
            mkdir -p "$T/p-tls"
            cat > "$T/p-tls/main.ty" <<EOF
package main
import "core:tls"

fn main():
    mode := args()[1]
    c := tls.connect("localhost", $tport)
    if tls.ok(c) == false:
        println("no server")
        return
    println("connected")
    tls.close_conn(c)
    if mode == "df":
        tls.close_conn(c)
    if mode == "uaf":
        println("wrote " + str(tls.write(c, to_bytes([65]))))
    println("survived")
EOF
            ./tychoc "$T/p-tls/main.ty" -o "$T/tls" >"$T/b" 2>&1 || { say_fail "tls: probe did not build"; sed 's/^/        /' "$T/b"; }
            export SSL_CERT_FILE="$T/ca.pem"
            if [ -x "$T/tls" ]; then
                run_leg "tls [ok]"  "$T/tls" ok  0 -                                              "survived"
                run_leg "tls [df]"  "$T/tls" df  1 "tycho: double free of tls connection handle"   "connected"
                run_leg "tls [uaf]" "$T/tls" uaf 1 "tycho: tls connection handle used after free"  "connected"
                controls tls "$T/p-tls" "connected" "tycho: double free of tls connection handle"
            fi
        else
            skipped="$skipped tls(the test server never came up)"
        fi
    else
        skipped="$skipped tls(could not mint a test certificate)"
    fi
else
    skipped="$skipped tls(missing: openssl)"
fi

if [ "$fail" -ne 0 ]; then
    echo "handle-guard: FAILED ($legs legs run)"
    exit 1
fi
if [ -n "$skipped" ]; then
    echo "handle-guard: $legs legs ok, SKIPPED --$skipped"
else
    echo "handle-guard: all green ($legs legs)"
fi
