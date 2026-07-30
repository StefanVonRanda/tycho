#!/bin/sh
# server/run.sh -- the gate `make server` never was.
#
# Every other runner in this tree (all eleven examples/*/run.sh) is
# batch-and-compare: run a program to completion, diff a golden. This one cannot
# be, because the program under test is a daemon: it never completes, it answers.
# So the pattern is different and this header says what it is, because nothing
# else in the tree does it.
#
#   readiness   server/main.ty:604-614 binds, asks the kernel for the port it
#               actually got (net.port_of) and prints a banner ON STDERR:
#                 tycho-httpd: serving <root> on http://<host>:<port>/ workers=N idle=Nms
#               We start with `--port 0` -- documented at server/main.ty:513 as
#               "0 = pick free" -- redirect stderr to a file, and poll that file
#               for the banner. One signal gives BOTH "it is listening" and
#               "which port", so the runner never picks a number that might be
#               taken and never sleeps a fixed interval hoping. A `sleep 1` is the
#               classic flake here and there is deliberately none in this file.
#               The banner is printed after net.listen() and before worker()
#               starts accepting (server/main.ty:616), so a connect in that window
#               is queued by the kernel rather than refused -- the socket is
#               already listening. That is the assumption this runner rests on and
#               it was proved by running the whole file ten times in a row.
#
#   teardown    trap on EXIT, unconditional. The server outlives this script
#               unless killed, and a stray tycho-httpd holding a port would
#               poison every later run on the box. It fires on success, on
#               failure, and on interrupt.
#
#   client      python3, raw sockets -- NOT curl. Three of the assertions cannot
#               be written with curl: the 20 KiB-header 431 needs the response
#               read WHILE the oversize head is still being written (see the
#               note on that case below), the 408 needs a head deliberately left
#               unfinished, and the 400 needs bytes that are not HTTP at all.
#               Having paid for a raw client for those, the rest use it too, so
#               there is one client and one failure vocabulary. python3 is
#               already a hard dependency of this tree (scripts/check_citations.py
#               is a gate), but the skip is here anyway, as examples/fetch/run.sh
#               skips on libcurl at its :32.
set -u
cd "$(dirname "$0")/.." || exit 2             # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "server: SKIP (python3 not installed)"; exit 0; }

T="$(mktemp -d)"
SRV=""
cleanup() {
    [ -n "$SRV" ] && kill -TERM "$SRV" 2>/dev/null
    [ -n "$SRV" ] && wait "$SRV" 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT INT TERM
fail=0

if ! "$TYCHOC" server/main.ty -o "$T/tycho-httpd" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc could not build server/main.ty"; sed 's/^/      /' "$T/build.log"; exit 1
fi

# The document root is a COPY of server/www, so the runner can add a case the
# repo cannot carry -- git stores no empty directory, and an empty directory is
# exactly what separates "301 to the slash form" from "404, no index.html there".
cp -R server/www "$T/www" || exit 2
mkdir -p "$T/www/emptydir" "$T/www/.hidden"
: > "$T/www/.hidden/secret.txt"

"$T/tycho-httpd" --root "$T/www" --host 127.0.0.1 --port 0 \
                 --workers 4 --idle-ms 500 >/dev/null 2>"$T/srv.err" &
SRV=$!

# ---- the conversation -------------------------------------------------------
python3 - "$T/srv.err" "$T/www" server/www <<'PY'
import os, re, select, socket, sys, threading, time

errlog, root, repo_www = sys.argv[1], sys.argv[2], sys.argv[3]
fails = []

def ok(name):   print("  ok   " + name)
def bad(name, got, want):
    fails.append(name)
    print("  FAIL " + name + "\n         got:  " + str(got) + "\n         want: " + str(want))
def eq(name, got, want):
    ok(name) if got == want else bad(name, got, want)

# A hard watchdog: every socket below has its own timeout, but a driver that
# wedges anyway must not hang the gate -- the shell's trap only runs when the
# script exits.
def watchdog():
    time.sleep(90); print("  FAIL watchdog: driver exceeded 90s"); os._exit(1)
threading.Thread(target=watchdog, daemon=True).start()

# ---- readiness: poll the stderr banner for the bound port -------------------
port, t0 = None, time.time()
while time.time() - t0 < 10.0:
    with open(errlog, "rb") as f:
        seen = f.read().decode("utf-8", "replace")
    m = re.search(r"^tycho-httpd: serving (\S+) on http://(\S+):(\d+)/  workers=(\d+) idle=(\d+)ms$",
                  seen, re.M)
    if m:
        port = int(m.group(3)); break
    time.sleep(0.02)
if port is None:
    print("  FAIL readiness: no startup banner on stderr within 10s")
    print("       stderr was: " + repr(seen[:500]))
    sys.exit(1)
ok("readiness: banner in %dms, bound port %d" % (round((time.time() - t0) * 1000), port))
eq("banner: root/workers/idle", (m.group(1), m.group(4), m.group(5)), (root, "4", "500"))
if port <= 0:
    bad("readiness: --port 0 resolved to a real port", port, "> 0")

def conn(timeout=6.0):
    return socket.create_connection(("127.0.0.1", port), timeout=timeout)

def exchange(req, timeout=6.0):
    """Send one request on a fresh fd, read until the peer closes."""
    s = conn(timeout)
    s.sendall(req)
    buf = b""
    try:
        while True:
            c = s.recv(65536)
            if not c: break
            buf += c
    except (socket.timeout, ConnectionResetError):
        pass
    s.close()
    return buf

def split(buf):
    if b"\r\n\r\n" not in buf: return buf.decode("latin1", "replace"), b""
    h, b = buf.split(b"\r\n\r\n", 1)
    return h.decode("latin1", "replace"), b

def status(buf):
    h, _ = split(buf)
    return h.split("\r\n")[0]

def header(buf, name):
    h, _ = split(buf)
    for line in h.split("\r\n")[1:]:
        k, _, v = line.partition(": ")
        if k.lower() == name.lower(): return v
    return None

def get(target, method=b"GET", extra=b""):
    return exchange(method + b" " + target + b" HTTP/1.1\r\nHost: t\r\n"
                    + extra + b"Connection: close\r\n\r\n")

# ---- 200, and the body is the file, byte for byte ---------------------------
r = get(b"/img/logo.png")
disk = open(os.path.join(repo_www, "img/logo.png"), "rb").read()
eq("200 /img/logo.png status",       status(r), "HTTP/1.1 200 OK")
eq("200 /img/logo.png Content-Type", header(r, "Content-Type"), "image/png")
eq("200 /img/logo.png Content-Length", header(r, "Content-Length"), str(len(disk)))
eq("200 /img/logo.png Cache-Control", header(r, "Cache-Control"), "no-cache")
eq("200 /img/logo.png body == file on disk (%d bytes)" % len(disk), split(r)[1], disk)

r = get(b"/")
idx = open(os.path.join(repo_www, "index.html"), "rb").read()
eq("200 / status",       status(r), "HTTP/1.1 200 OK")
eq("200 / Content-Type", header(r, "Content-Type"), "text/html; charset=utf-8")
eq("200 / serves index.html verbatim", split(r)[1], idx)

r = get(b"/style.css")
eq("200 /style.css Content-Type", header(r, "Content-Type"), "text/css; charset=utf-8")

# HEAD: the head of the GET, and not one byte of body.
r = get(b"/img/logo.png", method=b"HEAD")
eq("HEAD /img/logo.png status",         status(r), "HTTP/1.1 200 OK")
eq("HEAD /img/logo.png Content-Length", header(r, "Content-Length"), str(len(disk)))
eq("HEAD /img/logo.png body is empty",  split(r)[1], b"")

# ---- 301: a directory without the slash -------------------------------------
r = get(b"/about")
eq("301 /about status",   status(r), "HTTP/1.1 301 Moved Permanently")
eq("301 /about Location", header(r, "Location"), "/about/")
eq("200 /about/ after the redirect", status(get(b"/about/")), "HTTP/1.1 200 OK")

# An empty directory is a directory -- 301 to the slash form -- and only THEN a
# 404, for the index.html that is not in it. This is the pair the README still
# describes as a flat 404 (server/README.md, "Deliberately not implemented").
eq("301 /emptydir status",   status(get(b"/emptydir")), "HTTP/1.1 301 Moved Permanently")
eq("301 /emptydir Location", header(get(b"/emptydir"), "Location"), "/emptydir/")
eq("404 /emptydir/ (no index.html in it)", status(get(b"/emptydir/")), "HTTP/1.1 404 Not Found")

# ---- 403: the two ways out of the root --------------------------------------
# Sent raw, so the ../ survives to the server. This is the case the README's
# transcript needs `curl --path-as-is` for; a raw socket has no normalizer to
# turn off.
eq("403 traversal /../../etc/passwd", status(get(b"/../../etc/passwd")), "HTTP/1.1 403 Forbidden")
eq("403 traversal /a/../../..%2fetc", status(get(b"/a/../../../etc/passwd")), "HTTP/1.1 403 Forbidden")
eq("403 hidden segment /.hidden/secret.txt",
   status(get(b"/.hidden/secret.txt")), "HTTP/1.1 403 Forbidden")
eq("403 body is not the file", b"root:" in get(b"/../../etc/passwd").split(b"\r\n\r\n", 1)[1], False)

# ---- 404 / 405 --------------------------------------------------------------
eq("404 /nope.txt", status(get(b"/nope.txt")), "HTTP/1.1 404 Not Found")
r = get(b"/", method=b"POST")
eq("405 POST / status", status(r), "HTTP/1.1 405 Method Not Allowed")
eq("405 POST / Allow",  header(r, "Allow"), "GET, HEAD")
eq("405 DELETE /", status(get(b"/", method=b"DELETE")), "HTTP/1.1 405 Method Not Allowed")

# ---- 400: four different ways to not be a request ---------------------------
eq("400 binary junk", status(exchange(bytes(range(1, 32)) * 4 + b"\r\n\r\n")),
   "HTTP/1.1 400 Bad Request")
eq("400 absolute-form target",
   status(exchange(b"GET http://evil/ HTTP/1.1\r\nHost: t\r\n\r\n")), "HTTP/1.1 400 Bad Request")
eq("400 %00 control byte in path",
   status(exchange(b"GET /a%00b HTTP/1.1\r\nHost: t\r\n\r\n")), "HTTP/1.1 400 Bad Request")
# A Content-Length that is not a plain decimal is a smuggling primitive, refused
# rather than parsed leniently (server/main.ty:426-436).
eq("400 Content-Length: -5 (smuggling)",
   status(exchange(b"GET / HTTP/1.1\r\nHost: t\r\nContent-Length: -5\r\n\r\n")),
   "HTTP/1.1 400 Bad Request")

# ---- 431: a 20 KiB header against MAX_HEAD = 16384 --------------------------
# WHY THIS IS NOT A ONE-LINER: sendall() the whole 20 KiB and then recv() and you
# get ECONNRESET, not the 431. The server stops reading at MAX_HEAD, answers, and
# closes -- closing a socket with unread bytes still in its receive queue sends
# RST, and the RST discards the response that was already on the wire. So the
# read has to happen WHILE the write is still going. Measured: the naive form
# loses the 431 every time.
s = conn(3.0)
s.setblocking(False)
payload = b"GET / HTTP/1.1\r\nHost: t\r\nX-Big: " + b"A" * 20480 + b"\r\n\r\n"
sent, buf, t0 = 0, b"", time.time()
while time.time() - t0 < 5.0:
    r_, w_, _ = select.select([s], [s] if sent < len(payload) else [], [], 0.2)
    if r_:
        try:
            c = s.recv(65536)
        except ConnectionResetError:
            break
        if not c: break
        buf += c
        if b"\r\n\r\n" in buf: break
    if w_:
        try:
            sent += s.send(payload[sent:sent + 4096])
        except (BrokenPipeError, ConnectionResetError):
            break
s.close()
eq("431 20 KiB header (MAX_HEAD=16384)", status(buf), "HTTP/1.1 431 Request Header Fields Too Large")

# ---- 408: a head that never finishes ----------------------------------------
# --idle-ms 500 above, so this costs about half a second and is the reason the
# runner sets a short idle rather than taking the 5000ms default.
s = conn(6.0)
s.sendall(b"GET / HTTP/1.1\r\nHost: t\r\n")          # no terminating blank line
buf, t0 = b"", time.time()
try:
    while True:
        c = s.recv(4096)
        if not c: break
        buf += c
except (socket.timeout, ConnectionResetError):
    pass
waited = round((time.time() - t0) * 1000)
s.close()
eq("408 partial head, stalled (%dms, idle-ms=500)" % waited, status(buf), "HTTP/1.1 408 Request Timeout")
if not 400 <= waited <= 3000:
    bad("408 fired near the idle timeout", "%dms" % waited, "400..3000ms")

# ---- keep-alive: three requests, one fd -------------------------------------
s = conn(6.0)
served = []
def pump(sock, buf, want_end):
    """Read until want_end(buf); a closed peer is a failure, not a spin."""
    while not want_end(buf):
        c = sock.recv(65536)
        if not c: return None                        # server hung up: no reuse
        buf += c
    return buf
for target, path in ((b"/", "index.html"), (b"/style.css", "style.css"), (b"/data.json", "data.json")):
    try:
        s.sendall(b"GET " + target + b" HTTP/1.1\r\nHost: t\r\n\r\n")
        buf = pump(s, b"", lambda b: b"\r\n\r\n" in b)
        if buf is None:
            served.append("connection not reused for " + path); continue
        h, body = buf.split(b"\r\n\r\n", 1)
        n = int(re.search(rb"Content-Length: (\d+)", h).group(1))
        body = pump(s, body, lambda b, n=n: len(b) >= n)
        if body is None:
            served.append("body truncated for " + path); continue
        disk_b = open(os.path.join(repo_www, path), "rb").read()
        served.append(True if (h.startswith(b"HTTP/1.1 200") and body[:n] == disk_b) else "wrong response for " + path)
    except (socket.timeout, ConnectionResetError, BrokenPipeError) as e:
        served.append(type(e).__name__ + " on " + path)
s.close()
eq("keep-alive: 3 requests on one fd, 3 correct bodies", served, [True, True, True])

# ---- hostile disconnect: the SIGPIPE case the README records ----------------
# Before the fix, one peer vanishing mid-response took the whole process with it.
for _ in range(50):
    s = conn(3.0)
    s.sendall(b"GET /fonts/quicksand-regular.ttf HTTP/1.1\r\nHost: t\r\n\r\n")
    s.close()                                        # hang up without reading
eq("survives 50 hostile disconnects", status(get(b"/")), "HTTP/1.1 200 OK")

# ---- concurrency: force every worker to take a connection -------------------
# Eight sockets connected before any of them speaks. A worker is inside
# serve_conn until its connection closes, so with four accept loops and eight
# pending connections all four must accept one. Connection: close so the queued
# four are reached without waiting on the idle timeout.
socks = [conn(6.0) for _ in range(8)]
for s in socks:
    s.sendall(b"GET / HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n")
got = 0
for s in socks:
    buf = b""
    try:
        while True:
            c = s.recv(65536)
            if not c: break
            buf += c
    except (socket.timeout, ConnectionResetError):
        pass
    if buf.startswith(b"HTTP/1.1 200"): got += 1
    s.close()
eq("8 concurrent connections all answered 200", got, 8)

print("PORT=%d" % port)
sys.exit(1 if fails else 0)
PY
[ $? -eq 0 ] || fail=1

# ---- shutdown: SIGTERM, and the status the shell sees -----------------------
kill -TERM "$SRV" 2>/dev/null
wait "$SRV" 2>/dev/null
rc=$?
SRV=""
if [ "$rc" -eq 143 ]; then
    echo "  ok   SIGTERM: wait status 143 (128+SIGTERM)"
else
    echo "  FAIL SIGTERM: wait status $rc, want 143"; fail=1
fi

# ---- the access log, now that the server has stopped writing to it ----------
# One line per request on stderr: worker, client, method, target, status, bytes,
# duration (server/main.ty:342-352).
chk() {  # chk <name> <expected-count-test> <actual>
    if [ "$2" = "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1: got '$3', want '$2'"; fail=1; fi
}
workers=$(sed -n 's/^\(w[0-9][0-9]*\) .*/\1/p' "$T/srv.err" | sort -u | tr '\n' ' ')
chk "access log: every worker served (w1..w4)" "w1 w2 w3 w4 " "$workers"

peers=$(sed -n 's/^w[0-9][0-9]* \([^ ]*\) .*/\1/p' "$T/srv.err" | sort -u | tr '\n' ' ')
chk "access log: peer address on every line (net.peer_addr)" "127.0.0.1 " "$peers"

for code in 200 301 400 403 404 405 408 431; do
    n=$(grep -c " $code [0-9][0-9]* [0-9.]*ms" "$T/srv.err")
    if [ "$n" -gt 0 ]; then echo "  ok   access log: $n line(s) with status $code"
    else echo "  FAIL access log: no line with status $code"; fail=1; fi
done
# NOT asserted, and this is a finding rather than an omission: server/main.ty:617
# prints "tycho-httpd: stopped after N requests" after worker() returns, and that
# line is UNREACHABLE. Nothing in server/main.ty installs a SIGTERM or SIGINT
# handler (grepped: the only signal mentioned anywhere in the program or in
# corelib/net is SIGPIPE, and that is suppressed per-send with MSG_NOSIGNAL at
# corelib/net/net_shim.c:41-53). The default disposition therefore terminates the
# process where it stands, so the accept loops never wind down and the line never
# prints. Asserting its ABSENCE would be a gate that reddens the day someone
# fixes it, so what is asserted is only that the log ends in request lines --
# i.e. the process died serving, not mid-banner.
if [ -s "$T/srv.err" ] && tail -n 1 "$T/srv.err" | grep -q '^w[0-9]'; then
    echo "  ok   access log: last line is a request line (died serving)"
else
    echo "  FAIL access log: unexpected last line: $(tail -n 1 "$T/srv.err")"; fail=1
fi

# ---- the command line -------------------------------------------------------
"$T/tycho-httpd" --help >"$T/help.out" 2>&1
rc=$?
if [ "$rc" -eq 0 ] && grep -q '^  --port N ' "$T/help.out"; then
    echo "  ok   --help: exit 0, documents --port"
else
    echo "  FAIL --help: exit $rc"; sed 's/^/      /' "$T/help.out"; fail=1
fi
"$T/tycho-httpd" --bogus >"$T/bogus.out" 2>&1
rc=$?
if [ "$rc" -eq 1 ] && grep -q '^tycho-httpd: unknown option: --bogus$' "$T/bogus.out"; then
    echo "  ok   --bogus: exit 1, names the option"
else
    echo "  FAIL --bogus: exit $rc"; sed 's/^/      /' "$T/bogus.out"; fail=1
fi
"$T/tycho-httpd" --port 70000 >"$T/range.out" 2>&1
rc=$?
if [ "$rc" -eq 1 ] && grep -q '^tycho-httpd: --port must be 0..65535$' "$T/range.out"; then
    echo "  ok   --port 70000: exit 1, out of range"
else
    echo "  FAIL --port 70000: exit $rc"; sed 's/^/      /' "$T/range.out"; fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "server: OK"
else
    echo "server: FAIL"
fi
exit "$fail"
