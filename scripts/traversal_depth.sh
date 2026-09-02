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
ANS='s|    if hidden_answer(root, fsp):|    if false:|'
SJN='s|    fsp := path.resolve_under(root, rel)|    fsp := root + "/" + rel|'

build intact
probe intact "[1] all three guards intact"          403
build nohid "$HID"
probe nohid  "[2] hidden_segment defeated"     403
build nosj  "$SJN"
probe nosj   "[3] resolve_under defeated"     403
build noans "$ANS"
probe noans  "[4] hidden_answer defeated"     403
build none  "$HID" "$SJN" "$ANS"
probe none   "[C] control: ALL THREE defeated" LEAK

[ "$fail" -eq 0 ] || { echo "traversal-check: FAIL"; exit 1; }
echo "traversal-check: green (the traversal is refused with ANY ONE of the three guards removed, and gets through only when all three are -- so the defence is three independent layers, not one layer and two decorations)"
