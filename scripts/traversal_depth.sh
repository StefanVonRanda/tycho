#!/bin/sh
# Is the server's traversal defence still TWO layers, or has it quietly become one?
#
# `server/run.sh` already asserts that `/../../etc/passwd` and `/%2e%2e/...` are
# refused, and they are. What no lane in this tree can see is WHICH guard did it.
# `server/main.ty@resolve` has two independent ones -- `hidden_segment(path.clean(rel))`
# and `path.safe_join(root, rel)` -- and measured 2026-08-15 either alone refuses
# every traversal payload. That is real defence in depth and it is also the
# problem: **deleting one of them changes no observable behaviour**, so a
# regression that removes a layer passes every existing gate, and the next change
# removes the other. That is how two layers become zero in two commits neither of
# which looked wrong.
#
# So this lane defeats them ONE AT A TIME, in a COPY of server/main.ty, and
# requires the traversal to still be refused:
#
#   [1] both guards intact                  -> 403   (the shipped behaviour)
#   [2] hidden_segment defeated             -> 403   safe_join alone suffices
#   [3] safe_join defeated                  -> 403   hidden_segment alone suffices
#   [C] BOTH defeated                       -> 200 and the source LEAKS
#
# [C] is not decoration -- it is the whole lane. Without it, [1][2][3] all pass on
# a probe that never reached the server, which is exactly the blindness that made
# `core:http`'s certificate check ungatable until FRICTION #57 was fixed. The
# first version of this check WAS that failure: with only safe_join defeated the
# answer was still 403, and it looked like the guard was holding when in fact the
# other one had never been touched.
set -eu

cd "$(dirname "$0")/.."
T=$(mktemp -d)
SRV=""
cleanup() {
    if [ -n "$SRV" ]; then
        kill -TERM "$SRV" 2>/dev/null || true
        n=0
        while [ "$n" -lt 40 ] && kill -0 "$SRV" 2>/dev/null; do n=$((n + 1)); sleep 0.05; done
        kill -KILL "$SRV" 2>/dev/null || true
        SRV=""
    fi
    rm -rf "$T"
}
trap cleanup EXIT INT TERM

command -v python3 >/dev/null 2>&1 || { echo "traversal-check: SKIPPED (no python3)"; exit 0; }
[ -x ./tychoc ] || make tychoc >/dev/null

# A file OUTSIDE the served root that must never be reachable, with a marker no
# legitimate response carries.
mkdir -p "$T/root"
printf 'tycho-traversal-canary\n' > "$T/secret.txt"
printf '<h1>ok</h1>\n' > "$T/root/index.html"

fail=0
say() { printf '  %-42s %s\n' "$1" "$2"; }

# build a server copy with the named guards defeated; $1 = tag, rest = sed programs
build() {
    tag=$1; shift
    mkdir -p "$T/$tag"
    cp server/main.ty "$T/$tag/main.ty"
    for prog in "$@"; do
        # Assert the substitution APPLIED. A control that silently does not patch
        # reports the unmodified server as if it were the broken one.
        before=$(cksum < "$T/$tag/main.ty")
        sed -i "$prog" "$T/$tag/main.ty"
        after=$(cksum < "$T/$tag/main.ty")
        [ "$before" != "$after" ] || {
            echo "traversal-check: FAILED (patch did not apply for $tag: $prog)"; exit 1; }
    done
    ./tychoc "$T/$tag/main.ty" -o "$T/$tag/httpd" >"$T/$tag.log" 2>&1 || {
        echo "traversal-check: FAILED ($tag does not build)"; tail -4 "$T/$tag.log"; exit 1; }
}

# run one server copy and ask it for the canary through a traversal
probe() {
    tag=$1; label=$2; want=$3
    port=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
    "$T/$tag/httpd" --root "$T/root" --host 127.0.0.1 --port "$port" --workers 2 \
        >"$T/$tag.out" 2>&1 &
    SRV=$!
    got=$(python3 - "$port" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
for _ in range(200):
    s = socket.socket(); s.settimeout(0.2)
    if s.connect_ex(("127.0.0.1", port)) == 0:
        s.close(); break
    time.sleep(0.02)
else:
    print("NOSERVER"); sys.exit(0)
s = socket.socket(); s.settimeout(3); s.connect(("127.0.0.1", port))
s.sendall(b"GET /../secret.txt HTTP/1.1\r\nHost: a\r\n\r\n")
out = b""
try:
    while len(out) < 400:
        c = s.recv(400)
        if not c: break
        out += c
except socket.timeout:
    pass
s.close()
if b"tycho-traversal-canary" in out:
    print("LEAK")
else:
    line = out.split(b"\r\n")[0].decode("latin1") if out else "NORESPONSE"
    print(line.split(" ")[1] if " " in line else line)
PY
)
    cleanup_srv
    say "$label" "$got"
    [ "$got" != NOSERVER ] || { echo "  the server never came up -- this leg proves nothing"; fail=1; return; }
    if [ "$got" != "$want" ]; then
        if [ "$want" = LEAK ]; then
            echo "  CONTROL DEAD: with BOTH guards defeated the traversal was still refused,"
            echo "                so legs [1]-[3] are not measuring the guards at all."
        else
            echo "  LEAK: a layer was removed and the traversal got through -- the two guards"
            echo "        are no longer independently sufficient."
        fi
        fail=1
    fi
}
cleanup_srv() {
    if [ -n "$SRV" ]; then
        kill -TERM "$SRV" 2>/dev/null || true
        n=0
        while [ "$n" -lt 40 ] && kill -0 "$SRV" 2>/dev/null; do n=$((n + 1)); sleep 0.05; done
        kill -KILL "$SRV" 2>/dev/null || true
        SRV=""
    fi
}

HID='s|    if hidden_segment(path.clean(rel)):|    if false:|'
SJN='s|    fsp := path.safe_join(root, rel)|    fsp := root + "/" + rel|'

build intact
probe intact "[1] both guards intact"          403
build nohid "$HID"
probe nohid  "[2] hidden_segment defeated"     403
build nosj  "$SJN"
probe nosj   "[3] safe_join defeated"          403
build none  "$HID" "$SJN"
probe none   "[C] control: BOTH defeated"      LEAK

[ "$fail" -eq 0 ] || { echo "traversal-check: FAIL"; exit 1; }
echo "traversal-check: green (the traversal is refused with EITHER guard removed, and gets through only when both are -- so the defence is two independent layers, not one layer and a decoration)"
