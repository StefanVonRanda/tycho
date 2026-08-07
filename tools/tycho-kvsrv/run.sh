#!/bin/sh
# Gate for tycho-kvsrv, the concurrent HTTP key-value server in
# tools/tycho-kvsrv/. Seventh tool lane; nothing else RUNS a tool under
# tools/, and a daemon cannot be run-to-completion like the others -- it
# answers, it never finishes -- so this follows the server/run.sh pattern:
# start with --port 0, poll the stderr banner for the bound port, drive it
# with a raw-socket python client, assert the responses.
#
# WHAT IT ASSERTS:
#   [1] the round-trips: PUT /kv/alpha -> 200, GET -> 200 + the body,
#       GET missing -> 404, DELETE -> 200, GET after delete -> 404.
#   [2] the protocol: POST -> 405, a non-kv path -> 404.
#   [3] keep-alive: two requests on one connection both answered.
#   [4] THE CONCURRENCY PROBE: 4 parallel clients PUT distinct keys, all 4
#       GETs come back intact -- the actor store serializes the map ops, and
#       the assertion proves no command is dropped. (Race freedom is by
#       construction -- no shared storage -- but the probe proves the actor
#       round-trips don't lose messages under interleaving.)
#   [5] the transcript is golden-locked (expected.out).
#
# NO FIXED SLEEPS. Readiness = the banner on stderr (the socket is already
# listening; a connect in the window is queued by the kernel). The teardown
# trap kills the server on success, failure and interrupt alike.
set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-kvsrv: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-kvsrv/expected.out"
T="$(mktemp -d)"; trap "cp -r $T /tmp/kvsrv-debug 2>/dev/null; rm -rf $T" EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

SRV="$T/tycho-kvsrv"
if ! "$TYCHOC" tools/tycho-kvsrv/main.ty -o "$SRV" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-kvsrv: FAIL"; exit 1
fi

SERVPID=""
cleanup() {
    [ -n "$SERVPID" ] && kill -TERM "$SERVPID" 2>/dev/null
    [ -n "$SERVPID" ] && wait "$SERVPID" 2>/dev/null
}
trap cleanup EXIT INT TERM

"$SRV" --port 0 >/dev/null 2>"$T/srv.log" &
SERVPID=$!

# ---- readiness: poll the stderr banner for the bound port -------------------
port=""
i=0
while [ -z "$port" ] && [ "$i" -lt 200 ]; do
    port=$(grep -oE ':[0-9]+/' "$T/srv.log" 2>/dev/null | head -1 | tr -d ':/')
    [ -z "$port" ] && sleep 0.05
    i=$((i + 1))
done
if [ -z "$port" ]; then
    bad "no banner on stderr within 10s"; sed 's/^/      /' "$T/srv.log"
    echo "tycho-kvsrv: FAIL"; exit 1
fi

# ---- the assertions, one raw-socket client -----------------------------------
out="$T/all.out"
python3 - "$port" > "$out" 2>"$T/client.err" <<'PYEOF'
import socket, sys, threading
# Native-Windows python3 (what MSYS2 puts on PATH) opens stdout in TEXT mode,
# turning every \n into \r\n so this transcript stops matching its LF golden
# -- a diff whose two sides look identical. Force LF.
sys.stdout.reconfigure(newline="\n")
port = int(sys.argv[1])

def raw(method, path, body=b"", keep=False, times=1):
    """One connection, one or more requests, returns list of (status, body)."""
    s = socket.create_connection(("127.0.0.1", port))
    s.settimeout(5)
    conn = "keep-alive" if keep else "close"
    results = []
    for _ in range(times):
        hdr = "%s %s HTTP/1.1\r\nHost: x\r\nConnection: %s\r\n" % (method, path, conn)
        if body: hdr += "Content-Length: %d\r\n" % len(body)
        s.sendall(hdr.encode() + b"\r\n" + body)   # the blank line ends the head
        data = b""
        while b"\r\n\r\n" not in data:        # read the head first
            d = s.recv(4096)
            if not d: break
            data += d
        head, _, rest = data.partition(b"\r\n\r\n")
        lines = head.split(b"\r\n")
        status = lines[0].split(b" ")[1].decode()
        clen = 0
        for ln in lines[1:]:
            if ln.lower().startswith(b"content-length:"):
                clen = int(ln.split(b":")[1].strip())
        while len(rest) < clen:              # then exactly the body
            d = s.recv(4096)
            if not d: break
            rest += d
        results.append((status, rest[:clen].decode(errors="replace")))
        if not keep: break
    s.close()
    return results

print("PUT alpha: %s" % raw("PUT", "/kv/alpha", b"hello")[0][0])
print("GET alpha: %s %s" % (raw("GET", "/kv/alpha")[0][0], raw("GET", "/kv/alpha")[0][1]))
print("GET missing: %s" % raw("GET", "/kv/nope")[0][0])
print("DELETE alpha: %s" % raw("DELETE", "/kv/alpha")[0][0])
print("GET alpha after delete: %s" % raw("GET", "/kv/alpha")[0][0])
print("POST: %s" % raw("POST", "/kv/z", b"x")[0][0])
print("GET bad path: %s" % raw("GET", "/nope")[0][0])

# keep-alive: two requests, one connection
ka = raw("PUT", "/kv/ka", b"v1", keep=True, times=2)
print("keepalive r1: %s" % ka[0][0])
print("keepalive r2: %s %s" % (ka[1][0], ka[1][1]))

# the concurrency probe: 4 parallel PUTs, all 4 come back intact
def put(i):
    raw("PUT", "/kv/con%d" % i, ("val%d" % i).encode())
threads = [threading.Thread(target=put, args=(i,)) for i in range(4)]
for t in threads: t.start()
for t in threads: t.join()
intact = 0
for i in range(4):
    st, body = raw("GET", "/kv/con%d" % i)[0]
    if st == "200" and body == "val%d" % i:
        intact += 1
print("concurrency: %d/4 intact" % intact)
PYEOF
rc=$?
if [ "$rc" -ne 0 ]; then
    bad "client exited $rc"; sed 's/^/      /' "$T/client.err"
fi

if grep -q 'FAIL\|0/4 intact' "$out"; then
    bad "client printed a failure"; grep 'FAIL\|0/4' "$out" | sed 's/^/      /'
fi

if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-kvsrv"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-kvsrv/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-kvsrv: green (round-trips + 404/405 paths + keep-alive + 4-way concurrent PUT/GET all intact == golden)"
else
    echo "tycho-kvsrv: FAIL"; exit 1
fi
