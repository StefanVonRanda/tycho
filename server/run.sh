#!/bin/sh
# server/run.sh -- the gate `make server` never was.
#
# Every other runner in this tree (all eleven examples/*/run.sh) is
# batch-and-compare: run a program to completion, diff a golden. This one cannot
# be, because the program under test is a daemon: it never completes, it answers.
# So the pattern is different and this header says what it is, because nothing
# else in the tree does it.
#
#   readiness   server/main.ty@port_of binds and asks the kernel for the real
#               port (net.port_of); server/main.ty@banner prints it ON STDERR:
#                 tycho-httpd: serving <root> on http://<host>:<port>/ workers=N idle=Nms
#               We start with `--port 0` -- documented at server/main.ty@pick as
#               "0 = pick free" -- redirect stderr to a file, and poll that file
#               for the banner. One signal gives BOTH "it is listening" and
#               "which port", so the runner never picks a number that might be
#               taken and never sleeps a fixed interval hoping. A `sleep 1` is the
#               classic flake here and there is deliberately none in this file.
#               The banner is printed after net.listen() and before worker()
#               starts accepting (server/main.ty@worker), so a connect in that window
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
    [ -n "$SRV" ] && srv_kill
    [ -n "$SRV" ] && wait "$SRV" 2>/dev/null
    rm -rf "$T"
}
trap cleanup EXIT INT TERM
fail=0
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;; *) IS_WINDOWS=0 ;; esac

# ---- signalling the server, on either platform ------------------------------
#
# The shutdown cases need a real signal delivered to the server process. On
# POSIX that is `kill`. On Windows it is NOT: MSYS2's kill cannot signal a
# native PE, it TERMINATES it, so until 2026-08-09 the graceful path never ran
# and the lane BLOCKED waiting for a wind-down that could not happen (measured
# 2026-08-08: 43 minutes, which is worse than a red -- a hang stops the sweep).
#
# The Windows mechanism is a console control event, and it needs two things
# neither bash nor the corelib signal test's trick can provide here. First, the
# server must be a process-GROUP LEADER, or the event can only be sent to group
# 0 -- every process on this console, the harness shell included, which would
# Ctrl-Break the sweep itself. CREATE_NEW_PROCESS_GROUP is a CreationFlag, so
# it has to happen at spawn: server/winsignal.c does it. Second, the console
# API wants a WINDOWS pid, and `$!` is an MSYS pid -- different numbers for the
# same process -- so the launcher writes the real one to a file.
#
# The four helpers below are exactly today's `kill` calls on POSIX, so the
# Linux lane is unchanged line for line; only Windows takes another path.
# SIGTERM and SIGINT both map to CTRL_BREAK there, which is faithful:
# signal_shim.c's handler treats the console events alike, and its POSIX half
# installs the same handler for both signals.
WINSIG=""
SRVWIN=""

# srv_start <stderr-file> <args...>  -- launch $T/tycho-httpd, set SRV (and,
# on Windows, SRVWIN). The caller redirects stdout itself, as before.
srv_start() {
    _err="$1"; shift
    if [ "$IS_WINDOWS" = 1 ]; then
        rm -f "$T/winpid"
        "$WINSIG" spawn "$T/winpid" "$HTTPD" "$@" >/dev/null 2>"$_err" &
        SRV=$!
        # the launcher writes the pid before it waits; give it a bounded moment
        _i=0
        while [ ! -s "$T/winpid" ] && [ "$_i" -lt 100 ]; do sleep 0.1; _i=$((_i + 1)); done
        SRVWIN="$(cat "$T/winpid" 2>/dev/null)"
        [ -n "$SRVWIN" ] || { echo "  FAIL winsignal spawn never reported a pid"; fail=1; }
    else
        "$HTTPD" "$@" >/dev/null 2>"$_err" &
        SRV=$!
    fi
}

srv_sig()   { if [ "$IS_WINDOWS" = 1 ]; then "$WINSIG" break "$SRVWIN" 2>/dev/null
              else kill -"$1" "$SRV" 2>/dev/null; fi; }
srv_kill()  { if [ "$IS_WINDOWS" = 1 ]; then "$WINSIG" kill "$SRVWIN" 2>/dev/null
              else kill -KILL "$SRV" 2>/dev/null; fi; }
srv_alive() { if [ "$IS_WINDOWS" = 1 ]; then "$WINSIG" alive "$SRVWIN" 2>/dev/null
              else kill -0 "$SRV" 2>/dev/null; fi; }

if ! "$TYCHOC" server/main.ty -o "$T/tycho-httpd" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc could not build server/main.ty"; sed 's/^/      /' "$T/build.log"; exit 1
fi
# the Windows linker appends .exe; ask the filesystem rather than the platform
HTTPD="$T/tycho-httpd"
[ -f "$HTTPD.exe" ] && HTTPD="$HTTPD.exe"

if [ "$IS_WINDOWS" = 1 ]; then
    WINSIG="$T/winsignal.exe"
    if ! ${CC:-cc} -std=c11 -O2 -o "$WINSIG" server/winsignal.c >"$T/winsig.log" 2>&1; then
        echo "server: SKIP (cannot build server/winsignal.c -- no console-event sender)"
        sed 's/^/      /' "$T/winsig.log"
        exit 0
    fi
fi

# The document root is a COPY of server/www, so the runner can add a case the
# repo cannot carry -- git stores no empty directory, and an empty directory is
# exactly what separates "301 to the slash form" from "404, no index.html there".
cp -R server/www "$T/www" || exit 2
mkdir -p "$T/www/emptydir" "$T/www/.hidden"
: > "$T/www/.hidden/secret.txt"
# A zero-length file, for the one Range case a repo of real assets cannot carry:
# EVERY range over a 0-byte file is unsatisfiable, and `Content-Range: bytes */0`
# is the only thing a 416 can say about it. git stores the file happily enough,
# but it belongs beside emptydir -- both exist to make an empty thing testable.
: > "$T/www/empty.txt"

srv_start "$T/srv.err" --root "$T/www" --host 127.0.0.1 --port 0 \
          --workers 4 --idle-ms 500

# ---- the conversation -------------------------------------------------------
python3 - "$T/srv.err" "$T/www" server/www <<'PY'
import email.utils, os, re, select, socket, sys, threading, time

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

# ---- conditional GET: Last-Modified / If-Modified-Since ---------------------
# The document root is a COPY, so the runner OWNS these mtimes and can pin one to
# a fixed epoch. That is what turns Last-Modified from "some plausible string"
# into an exact byte comparison against a date python formatted independently --
# a formatter that agrees with itself proves nothing.
#
# A 304 is the server telling a client "what you hold is current", so being wrong
# in the permissive direction means a browser keeps a file that changed, silently,
# for as long as it caches. A suite that only checks "conditional GET -> 304" is
# passed by a server that answers 304 unconditionally, which is exactly the bug
# that matters. The older-stamp and unparseable-header cases below are the
# negative half, and they are the reason this section is longer than the feature.
MT = 1416470400                        # Thu, 20 Nov 2014 08:00:00 GMT, fixed
os.utime(os.path.join(root, "style.css"), (MT, MT))
LM = email.utils.formatdate(MT, usegmt=True)
css = open(os.path.join(repo_www, "style.css"), "rb").read()

def ims(v, method=b"GET"):
    return get(b"/style.css", method=method, extra=b"If-Modified-Since: " + v + b"\r\n")

r = get(b"/style.css")
eq("200 /style.css Last-Modified is the file's mtime", header(r, "Last-Modified"), LM)
eq("200 /style.css unconditional body", split(r)[1], css)
eq("200 /img/logo.png carries Last-Modified too",
   header(get(b"/img/logo.png"), "Last-Modified") is not None, True)

# Equal instants: the client holds exactly this version. "not newer" INCLUDES
# equal, and this is the assertion that pins `<=` against `<`.
r = ims(LM.encode())
eq("304 If-Modified-Since == mtime status",  status(r), "HTTP/1.1 304 Not Modified")
eq("304 If-Modified-Since == mtime no body", split(r)[1], b"")
# RFC 7230 3.3.2: the only Content-Length a 304 may carry is the one the 200
# would have sent, and this server answers 304 without opening the file, so it
# sends none. `Content-Length: 0` -- what the generic path would have invented --
# is the single value that rule forbids.
eq("304 sends no Content-Length",            header(r, "Content-Length"), None)
eq("304 sends no Content-Type",              header(r, "Content-Type"), None)
eq("304 still carries Last-Modified",        header(r, "Last-Modified"), LM)

eq("304 If-Modified-Since a day newer than mtime",
   status(ims(email.utils.formatdate(MT + 86400, usegmt=True).encode())),
   "HTTP/1.1 304 Not Modified")

# THE NEGATIVE CASE. One second older than the file: the client is stale and must
# be sent the bytes. A server hard-wired to 304 fails here and nowhere else.
r = ims(email.utils.formatdate(MT - 1, usegmt=True).encode())
eq("200 If-Modified-Since 1s older than mtime status", status(r), "HTTP/1.1 200 OK")
eq("200 If-Modified-Since 1s older than mtime body",   split(r)[1], css)

# An unparseable header is not a WEAKER condition, it is NO condition: 200 with
# the whole body, every time. The two obsolete RFC 7231 date forms land here by
# design -- not parsing them costs a needless 200, which is the safe direction.
for label, v in [("garbage",          b"not-a-date"),
                 ("rfc850 obsolete",  b"Sunday, 06-Nov-94 08:49:37 GMT"),
                 ("asctime obsolete", b"Thu Nov 20 08:00:00 2014"),
                 ("non-GMT zone",     b"Thu, 20 Nov 2014 08:00:00 UTC"),
                 ("impossible date",  b"Thu, 31 Feb 2014 08:00:00 GMT"),
                 ("hour 24",          b"Thu, 20 Nov 2014 24:00:00 GMT"),
                 ("empty value",      b"")]:
    rr = ims(v)
    eq("200 unparseable If-Modified-Since (%s) status" % label, status(rr), "HTTP/1.1 200 OK")
    eq("200 unparseable If-Modified-Since (%s) body"   % label, split(rr)[1], css)

# HEAD and 304 both suppress a body and they are NOT the same suppression: HEAD
# keeps the Content-Length a GET would have reported, a 304 has no length to
# report at all. Both meet on one request here.
r = ims(LM.encode(), method=b"HEAD")
eq("HEAD 304 status",            status(r), "HTTP/1.1 304 Not Modified")
# A CONTROL, not a proof, and it is labelled because the difference is invisible
# from the pass line: HEAD suppresses the body on its own, so no mutation of the
# conditional logic can redden this one. The 304-has-no-body CLAIM is carried by
# the GET form above, which reddens on the pre-change binary and on two mutants.
eq("HEAD 304 no body",           split(r)[1], b"")
eq("HEAD 304 no Content-Length", header(r, "Content-Length"), None)
eq("HEAD 200 still reports the file's Content-Length",
   header(get(b"/style.css", method=b"HEAD"), "Content-Length"), str(len(css)))

# ---- Range: the three forms, the 416, and the two interactions --------------
# EVERY CASE HERE COMPARES BYTES, never a length on its own. `Content-Length:
# 100` is satisfied by a server returning the WRONG hundred bytes -- an
# off-by-one on the inclusive end, a suffix read from the front, a slice taken
# from the wrong file -- and each of those is a real mutant of this feature. So
# the body is compared against the same slice sliced out of the file on disk,
# and the length assertions are there only to catch a Content-Length that
# describes the whole file while the body is the slice (or the reverse).
#
# logo.png, because it is BINARY: a wrong offset into a text file still looks
# like text, and a diff of two HTML fragments is easy to squint past.
RN = len(disk)                        # img/logo.png, read at the top of this file

def rng(v, target=b"/img/logo.png", method=b"GET", extra=b""):
    return get(target, method=method, extra=b"Range: " + v + b"\r\n" + extra)

# --- form 1: bytes=A-B, inclusive at BOTH ends.
r = rng(b"bytes=0-99")
eq("206 bytes=0-99 status",           status(r), "HTTP/1.1 206 Partial Content")
eq("206 bytes=0-99 Content-Range",    header(r, "Content-Range"), "bytes 0-99/%d" % RN)
eq("206 bytes=0-99 Content-Length is the SLICE not the file",
   header(r, "Content-Length"), "100")
eq("206 bytes=0-99 body == disk[0:100]", split(r)[1], disk[0:100])
eq("206 bytes=0-99 Content-Type is still the file's", header(r, "Content-Type"), "image/png")
eq("206 bytes=0-99 Accept-Ranges",    header(r, "Accept-Ranges"), "bytes")

# THE OFF-BY-ONE, pinned on its own. `bytes=0-0` is ONE byte, because A-B is
# inclusive at both ends. A server computing end - start sends zero bytes here
# and 99 in the case above -- and the case above would still show a plausible
# body, where this one cannot.
r = rng(b"bytes=0-0")
eq("206 bytes=0-0 is ONE byte (inclusive end)", header(r, "Content-Length"), "1")
eq("206 bytes=0-0 body == disk[0:1]",           split(r)[1], disk[0:1])
eq("206 bytes=0-0 Content-Range",               header(r, "Content-Range"), "bytes 0-0/%d" % RN)

# An INTERIOR slice: neither end is a boundary of the file, so a server that
# ignores `start` and serves from 0 fails here and passes bytes=0-99.
r = rng(b"bytes=1000-1099")
eq("206 bytes=1000-1099 body == disk[1000:1100]", split(r)[1], disk[1000:1100])
eq("206 bytes=1000-1099 Content-Range", header(r, "Content-Range"), "bytes 1000-1099/%d" % RN)

# B past EOF is CLAMPED, not refused: the range overlaps the file, so it is
# satisfiable, and the Content-Range reports what was actually sent.
r = rng(b"bytes=%d-999999" % (RN - 10))
eq("206 bytes=A-B with B past EOF clamps", split(r)[1], disk[RN - 10:])
eq("206 clamped Content-Range names the real end",
   header(r, "Content-Range"), "bytes %d-%d/%d" % (RN - 10, RN - 1, RN))

# The whole file, asked for as a range, is a 206 and not a 200.
r = rng(b"bytes=0-%d" % (RN - 1))
eq("206 bytes=0-LAST status", status(r), "HTTP/1.1 206 Partial Content")
eq("206 bytes=0-LAST body == whole file", split(r)[1], disk)

# --- form 2: bytes=A-, open-ended.
r = rng(b"bytes=%d-" % (RN - 50))
eq("206 bytes=A- status",         status(r), "HTTP/1.1 206 Partial Content")
eq("206 bytes=A- Content-Length", header(r, "Content-Length"), "50")
eq("206 bytes=A- Content-Range",  header(r, "Content-Range"),
   "bytes %d-%d/%d" % (RN - 50, RN - 1, RN))
eq("206 bytes=A- body == disk[-50:]", split(r)[1], disk[RN - 50:])
eq("206 bytes=0- is the whole file, as a 206", split(rng(b"bytes=0-"))[1], disk)

# --- form 3: bytes=-N, the LAST N bytes. The only form whose START depends on
# the length, and the one a naive implementation reads from the FRONT.
r = rng(b"bytes=-64")
eq("206 bytes=-64 status",         status(r), "HTTP/1.1 206 Partial Content")
eq("206 bytes=-64 Content-Length", header(r, "Content-Length"), "64")
eq("206 bytes=-64 Content-Range",  header(r, "Content-Range"),
   "bytes %d-%d/%d" % (RN - 64, RN - 1, RN))
eq("206 bytes=-64 body == the LAST 64 bytes", split(r)[1], disk[RN - 64:])
# ...and it is NOT the first 64, which is the whole point of the case above.
eq("206 bytes=-64 body != the FIRST 64 bytes", split(r)[1] == disk[0:64], False)
# A suffix longer than the file is the whole file (RFC 7233 2.1), not a 416.
r = rng(b"bytes=-999999")
eq("206 bytes=-N with N > length is the whole file", split(r)[1], disk)
eq("206 bytes=-N oversize Content-Range", header(r, "Content-Range"), "bytes 0-%d/%d" % (RN - 1, RN))

# --- 416: syntactically fine, but no such bytes.
for label, v in [("first-byte-pos == length", b"bytes=%d-" % RN),
                 ("first-byte-pos past EOF",  b"bytes=999999-1000000"),
                 ("zero-length suffix",       b"bytes=-0")]:
    r = rng(v)
    eq("416 %s status" % label, status(r), "HTTP/1.1 416 Range Not Satisfiable")
    eq("416 %s Content-Range is bytes */LEN" % label,
       header(r, "Content-Range"), "bytes */%d" % RN)
    eq("416 %s body is not the file" % label, split(r)[1] == disk, False)

# A 0-byte file: every range over it is unsatisfiable, and */0 is all a 416 can
# say. The empty file also proves the plain path still works on it.
eq("416 any range over a 0-byte file", status(rng(b"bytes=0-", target=b"/empty.txt")),
   "HTTP/1.1 416 Range Not Satisfiable")
eq("416 over a 0-byte file says bytes */0",
   header(rng(b"bytes=0-", target=b"/empty.txt"), "Content-Range"), "bytes */0")
# A CONTROL: no mutation of the range code can redden this, and a server with no
# Range support passes it. It is here so that the two 416s above are read against
# a file that IS served correctly without one -- otherwise "416 on empty.txt"
# would also pass for a server that cannot serve empty.txt at all.
eq("200 the 0-byte file itself", header(get(b"/empty.txt"), "Content-Length"), "0")

# --- an unusable Range is IGNORED: 200, whole file, never an error. RFC 7233
# 2.1. That includes the multipart form, which this server does not implement
# and answers with everything rather than with a 416 or a 400.
#
# The first two are CONTROLS and the rest are proofs, which is not visible from
# the pass line. A header whose unit this server does not recognise is, by
# construction, the same input as NO Range header -- both leave parse_range at
# the same `return RANGE_NONE` -- so no mutant can separate them from an
# ordinary 200. The other ten reach the byte-range grammar and redden under a
# server that REJECTS what it cannot parse instead of ignoring it, which is the
# usual misreading of RFC 7233 2.1 and the bug these ten exist to catch.
for label, v in [("no unit",           b"0-99"),
                 ("unknown unit",      b"items=0-99"),
                 ("empty spec",        b"bytes="),
                 ("no digits",         b"bytes=abc"),
                 ("bare dash",         b"bytes=-"),
                 ("no dash",           b"bytes=99"),
                 ("last < first",      b"bytes=99-0"),
                 ("negative first",    b"bytes=-5-9"),
                 ("trailing garbage",  b"bytes=1-2-3"),
                 ("leading space",     b"bytes= 0-99"),
                 ("hex",               b"bytes=0x10-0x20"),
                 ("MULTIPART",         b"bytes=0-99,200-299")]:
    r = rng(v)
    eq("200 unusable Range (%s) status" % label, status(r), "HTTP/1.1 200 OK")
    eq("200 unusable Range (%s) whole body" % label, split(r)[1], disk)

# The range unit is a token, so its case is not significant.
eq("206 BYTES=0-9 (unit is case-insensitive)",
   status(rng(b"BYTES=0-9")), "HTTP/1.1 206 Partial Content")

# A digit run too long to be a position is not a parse failure with a wild
# value in it -- it lands in the arm the true number would have.
eq("416 first-byte-pos of 26 digits (past any EOF)",
   status(rng(b"bytes=99999999999999999999999999-")), "HTTP/1.1 416 Range Not Satisfiable")
eq("206 last-byte-pos of 26 digits clamps to the file",
   split(rng(b"bytes=0-99999999999999999999999999"))[1], disk)

# --- INTERACTION 1: Range + If-Modified-Since. RFC 7232 6 evaluates the
# conditional FIRST and a 304 wins -- the client is being told "what you hold is
# current", which answers a request for part of it as fully as one for all of it.
# A 206 here would hand back bytes the client already has and drop the 304.
# style.css is the file with the pinned mtime, so LM is exact.
r = get(b"/style.css", extra=b"If-Modified-Since: " + LM.encode() + b"\r\nRange: bytes=0-9\r\n")
eq("304 outranks Range (conditional first) status", status(r), "HTTP/1.1 304 Not Modified")
eq("304 outranks Range: no body",          split(r)[1], b"")
eq("304 outranks Range: no Content-Range", header(r, "Content-Range"), None)
eq("304 outranks Range: no Accept-Ranges", header(r, "Accept-Ranges"), None)
# ...and when the conditional does NOT fire, the range still does. Same request,
# a stamp one second older than the file.
r = get(b"/style.css",
        extra=b"If-Modified-Since: " + email.utils.formatdate(MT - 1, usegmt=True).encode()
              + b"\r\nRange: bytes=0-9\r\n")
eq("206 when the conditional does not fire", status(r), "HTTP/1.1 206 Partial Content")
eq("206 with a stale If-Modified-Since serves the slice", split(r)[1], css[0:10])

# --- INTERACTION 2: Range + HEAD. The head its GET would have produced: 206,
# the same Content-Range, a Content-Length of the SLICE, and no bytes. This is a
# third body-suppressing case beside HEAD-on-200 and 304, and it needs no third
# rule in emit() -- 206 is not bodyless, so the HEAD arm reports the slice's
# length exactly as it reports the file's on a 200.
r = rng(b"bytes=0-99", method=b"HEAD")
eq("HEAD 206 status",           status(r), "HTTP/1.1 206 Partial Content")
eq("HEAD 206 Content-Range",    header(r, "Content-Range"), "bytes 0-99/%d" % RN)
eq("HEAD 206 Content-Length is the slice", header(r, "Content-Length"), "100")
# A CONTROL, labelled for the reason "HEAD 304 no body" above is: HEAD suppresses
# a body on its own, so no mutation of the RANGE logic can redden this one. The
# claim it looks like it carries -- that a 206 is a slice and not the file -- is
# carried by the GET forms above, which redden on the pre-change binary and on
# three mutants. What this line DOES prove is that the 206 did not slip past the
# HEAD arm, which the Content-Length assertion beside it cannot say alone.
eq("HEAD 206 no body",          split(r)[1], b"")
eq("HEAD 416 status", status(rng(b"bytes=999999-", method=b"HEAD")),
   "HTTP/1.1 416 Range Not Satisfiable")
eq("HEAD 416 Content-Range", header(rng(b"bytes=999999-", method=b"HEAD"), "Content-Range"),
   "bytes */%d" % RN)

# Accept-Ranges is advertised on the two responses that describe this file's
# bytes and on neither of the two that do not.
eq("200 advertises Accept-Ranges", header(get(b"/style.css"), "Accept-Ranges"), "bytes")
eq("304 does not advertise Accept-Ranges", header(ims(LM.encode()), "Accept-Ranges"), None)
eq("404 does not advertise Accept-Ranges", header(get(b"/nope.txt"), "Accept-Ranges"), None)

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
# rather than parsed leniently (server/main.ty:463-473).
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

# ---- shutdown cases 1-6 + the access-log tail -------------------------------
# These ran on POSIX only until 2026-08-09: every one drives the server with a
# real signal, and MSYS2's kill cannot deliver one to a native PE -- it
# TERMINATES it, so the graceful path never ran and the lane BLOCKED waiting
# for a wind-down that could not happen (measured 2026-08-08: 43 minutes, worse
# than a red because a hang stops the whole sweep). They now run on both
# platforms through srv_sig/srv_kill/srv_alive; see the header of those helpers
# for why Windows needs a launcher and a second pid.
#
# NOTE the body below is deliberately NOT indented: it carries unindented
# heredoc terminators (`PY`), which only work at column 0.
# ---- shutdown case 1 of 2: SIGTERM is a CLEAN shutdown ----------------------
# This block asserted wait status 143 until the signals plan -- that is, it
# asserted the server was KILLED, that server/main.ty's last line was
# unreachable, and that the accept loops never wound down. server/main.ty now
# arms core:signal with the listening fd before it prints the banner, so SIGTERM
# calls shutdown(fd, SHUT_RDWR) under all four accept loops at once and the
# process leaves through its own bottom.
#
# WHY EXIT 0 IS THE ALL-WORKERS-EXITED ASSERTION and not merely a tidier status:
# worker() (server/main.ty:574-579) spawns peer k+1, runs accept loop k itself,
# and returns `n + wait(peer)`; main() CALLS worker() rather than spawning it. So
# everything below the fan-out -- the stopped line included -- is reachable only
# after every spawned peer has been joined. One loop still blocked in accept(2)
# means wait() never returns, which means no line and no exit. Paired with the
# "every worker served (w1..w4)" assertion below, which proves all four loops
# were live and serving, exit 0 IS "all four were released".
#
# The watchdog is what stops a regression HANGING this gate instead of failing
# it: on a hang the server is SIGKILLed after 10s and the status comes back 137.
#
# THE REDIRECT IS LOAD-BEARING, not tidiness. `kill "$WD"` below reaps the
# subshell but NOT the `sleep` it is blocked in, and the orphaned sleep inherits
# this script's stdout. A caller that CAPTURES output -- `out=$(sh server/run.sh)`,
# which is how a CI step collecting a log does it -- holds the pipe open until
# every writer closes it, so `$(...)` blocks on the orphan rather than on the
# script. Measured on this file, before the redirect: 7089 ms direct, 14280 ms
# captured -- the 7.2 s gap is the remainder of a 10 s sleep armed part-way in.
# The other three watchdogs (server/run.sh:512, server/run.sh:586,
# server/run.sh:632) were written this way from the start; this one was the
# outlier.
( sleep 10; srv_kill ) >/dev/null 2>&1 &
WD=$!
srv_sig TERM
wait "$SRV" 2>/dev/null
rc=$?
kill "$WD" 2>/dev/null
wait "$WD" 2>/dev/null
SRV=""
if [ "$rc" -eq 0 ]; then
    echo "  ok   SIGTERM: exit status 0 (clean; every accept loop returned and was joined)"
elif [ "$rc" -eq 137 ]; then
    echo "  FAIL SIGTERM: watchdog SIGKILL after 10s -- a worker never left accept()"; fail=1
else
    echo "  FAIL SIGTERM: wait status $rc, want 0 (143 = the pre-phase-3 behaviour)"; fail=1
fi

# The line at the bottom of server/main.ty's main(). Until phase 3 it was
# unreachable, and this file carried a paragraph of comment where this assertion
# belongs.
stopped=$(grep '^tycho-httpd: stopped after [0-9][0-9]* requests$' "$T/srv.err")
if [ -n "$stopped" ]; then
    echo "  ok   SIGTERM: $stopped"
else
    echo "  FAIL SIGTERM: no 'stopped after N requests' line on stderr"; fail=1
fi

# ---- the access log, now that the server has stopped writing to it ----------
# One line per request on stderr: worker, client, method, target, status, bytes,
# duration (server/main.ty:354-364).
chk() {  # chk <name> <expected-count-test> <actual>
    if [ "$2" = "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1: got '$3', want '$2'"; fail=1; fi
}
workers=$(sed -n 's/^\(w[0-9][0-9]*\) .*/\1/p' "$T/srv.err" | sort -u | tr '\n' ' ')
chk "access log: every worker served (w1..w4)" "w1 w2 w3 w4 " "$workers"

peers=$(sed -n 's/^w[0-9][0-9]* \([^ ]*\) .*/\1/p' "$T/srv.err" | sort -u | tr '\n' ' ')
chk "access log: peer address on every line (net.peer_addr)" "127.0.0.1 " "$peers"

for code in 200 206 301 400 403 404 405 408 416 431; do
    n=$(grep -c " $code [0-9][0-9]* [0-9.]*ms" "$T/srv.err")
    if [ "$n" -gt 0 ]; then echo "  ok   access log: $n line(s) with status $code"
    else echo "  FAIL access log: no line with status $code"; fail=1; fi
done
# The served count is the sum every accept loop returned, so it is the second
# reading on the same fact as exit 0: a loop that never returned contributes
# nothing, and its requests would go missing from the total. One access log line
# per request (server/main.ty:354-364), so the two numbers must agree.
n_served=$(printf '%s\n' "$stopped" | sed -n 's/^tycho-httpd: stopped after \([0-9]*\) requests$/\1/p')
n_logged=$(grep -c '^w[0-9]' "$T/srv.err")
chk "SIGTERM: served count == access log lines" "$n_logged" "${n_served:-none}"

# The log used to end in a request line, because the process died serving. It now
# ends in the shutdown line, and that ordering matters: it says the count was
# printed AFTER the last request was logged, not from some half-wound-down state.
if [ -s "$T/srv.err" ] && tail -n 1 "$T/srv.err" | grep -q '^tycho-httpd: stopped after'; then
    echo "  ok   access log: last line is the shutdown line (stopped after serving)"
else
    echo "  FAIL access log: unexpected last line: $(tail -n 1 "$T/srv.err")"; fail=1
fi

# A second server, started the same way and polled for the same banner. Used by
# both remaining cases; there is only one process to kill per case, and the first
# one is already gone.
respawn() {  # respawn <errfile> [idle-ms]; sets SRV
    srv_start "$1" --root "$T/www" --host 127.0.0.1 --port 0 \
              --workers 4 --idle-ms "${2:-500}"
    i=0
    while [ "$i" -lt 500 ]; do                   # the banner, not a fixed sleep
        grep -q '^tycho-httpd: serving ' "$1" 2>/dev/null && return 0
        i=$((i + 1)); sleep 0.02
    done
    echo "  FAIL readiness: respawned server printed no banner within 10s"; fail=1
}

# ---- shutdown case 2 of 3: SIGINT, the other signal core:signal installs -----
# corelib/signal/signal_shim.c arms SIGTERM and SIGINT with the same handler, and
# core:signal's own fixture can only exercise SIGTERM -- glibc's system(3) sets
# SIGINT to SIG_IGN in the caller for the duration of the call, so the fixture's
# `kill -INT $PPID` would be swallowed by system() rather than by anything under
# test. This is where the second half of that pair gets its only real exercise:
# an operator's Ctrl-C must wind the server down exactly as SIGTERM does.
respawn "$T/int.err"
srv_sig INT
wait "$SRV" 2>/dev/null
rc=$?
SRV=""
if [ "$rc" -eq 0 ] && grep -q '^tycho-httpd: stopped after [0-9]' "$T/int.err"; then
    echo "  ok   SIGINT: exit status 0 and the stopped line (same handler as SIGTERM)"
else
    echo "  FAIL SIGINT: wait status $rc, want 0 with a stopped line"; fail=1
    sed 's/^/      /' "$T/int.err"
fi

# ---- shutdown case 3 of 3: SIGKILL is still abrupt, and still tested ---------
# Not deleted, just no longer the same case. SIGKILL cannot be caught, so no
# handler runs, nothing winds down, and the stopped line is never printed. That
# is correct behaviour, and it is the CONTROL for case 1: it shows the clean exit
# above comes from the handler core:signal installed and not from something the
# process would have done on the way out of any signal at all.
respawn "$T/kill.err"
srv_kill
wait "$SRV" 2>/dev/null
rc=$?
SRV=""
if [ "$rc" -eq 137 ]; then
    echo "  ok   SIGKILL: wait status 137 (128+SIGKILL), uncatchable by design"
else
    echo "  FAIL SIGKILL: wait status $rc, want 137"; fail=1
fi
if grep -q '^tycho-httpd: stopped after' "$T/kill.err"; then
    echo "  FAIL SIGKILL: printed the stopped line -- no handler can have run"; fail=1
else
    echo "  ok   SIGKILL: no stopped line, nothing wound down (the control for case 1)"
fi

# The bound port out of a respawned server's banner, same regex as the driver's.
port_of() {
    sed -n 's|^tycho-httpd: serving .* on http://[^:]*:\([0-9]*\)/.*|\1|p' "$1" | head -n 1
}

# ---- case 4: a TRANSIENT accept failure must not retire a worker ------------
# the signals plan. Until batch A, accept_loop's Err arm was an unconditional
# `running = false`, so ONE EMFILE retired that accept loop for the life of the
# process -- and with every loop retired the server left through its own bottom
# with no signal at all, printing the stopped line and exiting 0 while the
# operator was told nothing except that it had gone. That is the exact shape this
# asserts against: after a transient fd exhaustion that has since been LIFTED, the
# server must still be running and must still answer.
#
# The exhaustion is induced with prlimit(2) on the live process rather than with a
# storm of connections, because a storm cannot do it: with four accept loops the
# server never holds more than four connections at once, so its own fd budget is
# never what runs out. Lowering the SOFT limit only (the hard limit is left alone,
# or it could not be raised back without CAP_SYS_RESOURCE) makes the next accept
# fail with EMFILE and nothing else change.
#
# WHY --idle-ms 200 AND A WINDOW LONGER THAN IT, which is the whole reason this
# case is not three lines. A thread already blocked in accept(2) has ALREADY
# passed the rlimit check: __sys_accept4 reserves the descriptor with
# get_unused_fd_flags() and only then blocks in do_accept(). So lowering the limit
# under four parked accept loops changes nothing for those four calls -- they
# complete and install fds 4..7 over a soft limit of 4. EMFILE is reached only
# when a loop enters accept(2) AFRESH, which happens after its connection closes,
# which for a client that never speaks is one idle timeout. Measured directly:
# with idle 500 and a 400ms window the loops never re-enter accept and NOTHING
# fails, on patched and unpatched alike; with the timeout inside the window the
# unpatched server retires all four loops and exits 0 on its own every time.
respawn "$T/emfile.err" 200
# `>/dev/null` on the watchdog is not tidiness. `kill "$WD"` reaps the subshell
# but NOT the `sleep` it is blocked in, and an orphaned sleep still holds the
# stdout this script inherited -- so a caller that captures output, as
# `out=$(sh server/run.sh)` does, blocks until the longest sleep expires rather
# than until the script exits. Measured: 4.4s per run direct, 34.5s per run
# captured, entirely the orphan. Closing its stdout costs nothing and removes it.
( sleep 15; srv_kill ) >/dev/null 2>&1 &
WD=$!
python3 - "$SRV" "$(port_of "$T/emfile.err")" <<'PY'
import socket, sys, time
try:
    import resource                      # POSIX only -- absent on Windows, where
except ImportError:                      # the IMPORT is the failure, not prlimit
    print("  skip transient-accept: no `resource` module (Windows -- RLIMIT_NOFILE"
          " has no equivalent, so the EMFILE window cannot be forced)"); sys.exit(0)
pid, port = int(sys.argv[1]), int(sys.argv[2])
if not hasattr(resource, "prlimit"):
    print("  skip transient-accept: resource.prlimit unavailable (needs Linux)"); sys.exit(0)
try:
    soft, hard = resource.prlimit(pid, resource.RLIMIT_NOFILE)
except (OSError, PermissionError) as e:
    print("  skip transient-accept: prlimit on the server denied (%s)" % e); sys.exit(0)

def get(timeout=3.0):
    s = socket.create_connection(("127.0.0.1", port), timeout)
    s.settimeout(timeout)
    s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    buf = b""
    while b"\r\n\r\n" not in buf:
        d = s.recv(65536)
        if not d:
            break
        buf += d
    s.close()
    return buf

if not get().startswith(b"HTTP/1.1 200"):
    print("  FAIL transient-accept: server was not serving before the test"); sys.exit(1)
time.sleep(0.3)                              # every loop back in accept(2)
resource.prlimit(pid, resource.RLIMIT_NOFILE, (4, hard))   # 0,1,2 + the listener
storm = []
for _ in range(8):                           # four get taken, four stay queued
    try:
        storm.append(socket.create_connection(("127.0.0.1", port), 1.0))
    except OSError:
        break
time.sleep(0.8)                              # > the 200ms idle, so the loops
                                             # re-enter accept(2) inside the window
try:
    resource.prlimit(pid, resource.RLIMIT_NOFILE, (soft, hard))   # lifted again
except ProcessLookupError:
    # The pre-batch-A failure, and the reason this case exists: no signal was
    # sent and no error was reported, the accept loops simply retired one by one
    # and main() walked out of its own bottom printing the stopped line.
    print("  FAIL transient-accept: the server EXITED during the EMFILE window")
    print("       no signal was sent -- every accept loop retired on a transient error")
    sys.exit(1)
for s in storm:
    try:
        s.close()
    except OSError:
        pass
time.sleep(0.6)
try:
    body = get()
except OSError as e:
    print("  FAIL transient-accept: server unreachable after the EMFILE window (%s)" % e)
    sys.exit(1)
if body.startswith(b"HTTP/1.1 200"):
    print("  ok   transient accept failure (EMFILE, window lifted): server still serving")
else:
    print("  FAIL transient-accept: got %r, want a 200" % body[:40]); sys.exit(1)
PY
rc=$?
kill "$WD" 2>/dev/null
wait "$WD" 2>/dev/null
[ "$rc" -eq 0 ] || fail=1
# ...and the accept loops that survived it are still able to wind DOWN, which is
# the half a retry loop can silently cost. Same assertion as case 1, on a server
# that has been through the EMFILE window. `kill -0` FIRST, or this passes
# vacuously against a server that already drained: a dead pid takes the signal
# without complaint and `wait` hands back the exit status it had anyway.
if ! srv_alive; then
    echo "  FAIL after EMFILE: server was already gone before SIGTERM (the pool drained)"; fail=1
fi
( sleep 10; srv_kill ) >/dev/null 2>&1 &
WD=$!
srv_sig TERM
wait "$SRV" 2>/dev/null
rc=$?
kill "$WD" 2>/dev/null
wait "$WD" 2>/dev/null
SRV=""
if [ "$rc" -eq 0 ] && grep -q '^tycho-httpd: stopped after [0-9]' "$T/emfile.err"; then
    echo "  ok   after EMFILE: SIGTERM still exits 0 with the stopped line (retry did not eat the shutdown)"
else
    echo "  FAIL after EMFILE: wait status $rc, want 0 with a stopped line"; fail=1
    sed 's/^/      /' "$T/emfile.err"
fi

# ---- case 5: shutdown must not wait out a BUSY keep-alive connection --------
# the signals plan. serve_conn's keep-alive loop now tests
# signal.shutdown_requested() in its loop condition, so a worker stops between
# requests instead of serving its peer until MAX_REQS. Measured on the four-client
# drip below: 102215 ms before, 8 ms after. The 10s watchdog is therefore not a
# margin, it is a cliff -- the pre-batch-A behaviour misses it by two orders of
# magnitude, so a regression FAILS here rather than merely getting slower.
#
# NOTE this is the busy case, not the parked-idle one. A worker already blocked in
# a read when the signal lands still waits out SO_RCVTIMEO; that is phase 19 and
# no assertion here claims otherwise.
respawn "$T/busy.err"
# Four connections, each sending a fresh request every 100ms forever. The server
# has four workers, so every one of them ends up in serve_conn's keep-alive loop.
python3 - "$(port_of "$T/busy.err")" >"$T/drip.out" 2>&1 <<'PY' &
import socket, sys, threading, time
port = int(sys.argv[1])
def drip():
    s = socket.create_connection(("127.0.0.1", port), 3.0)
    s.settimeout(3.0)
    while True:
        s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n")
        if not s.recv(65536):
            return
        time.sleep(0.1)
for _ in range(4):
    threading.Thread(target=drip, daemon=True).start()
time.sleep(60)
PY
DRIP=$!
sleep 1
( sleep 10; srv_kill ) >/dev/null 2>&1 &
WD=$!
srv_sig TERM
wait "$SRV" 2>/dev/null
rc=$?
kill "$WD" 2>/dev/null
wait "$WD" 2>/dev/null
kill "$DRIP" 2>/dev/null
wait "$DRIP" 2>/dev/null
SRV=""
if [ "$rc" -eq 0 ] && grep -q '^tycho-httpd: stopped after [0-9]' "$T/busy.err"; then
    echo "  ok   SIGTERM under 4 busy keep-alive clients: exit 0 well inside the 10s watchdog"
else
    echo "  FAIL SIGTERM under load: wait status $rc, want 0 (137 = it served the clients instead)"; fail=1
    tail -n 3 "$T/busy.err" | sed 's/^/      /'
fi

# ---- case 6: shutdown must not wait out a PARKED keep-alive connection ------
# the signals plan, and the other half of case 5. Case 5 covers a worker that
# REACHES serve_conn's loop condition between requests; this one covers a worker
# already blocked INSIDE read_request_capped when the signal lands, which cannot
# reach that condition at all. Shutting down the listener wakes accept(2) and
# nothing else, so before phase 19 such a worker sat out its full SO_RCVTIMEO:
# measured 4878 ms at --idle-ms 5000, four parked clients. server/main.ty now
# registers each accepted fd with core:signal (server/run.sh's sibling assertion
# is corelib/signal/signal.ty's register_conn) so the handler shuts those down
# too: 4878 ms -> 1 ms, 5 runs of 5.
#
# THE WATCHDOG IS THE ASSERTION, and the numbers are chosen so it cannot be a
# coin flip: --idle-ms 8000 against a 3s watchdog. The pre-phase-19 behaviour
# cannot finish in under 8 s by any path, so it comes back 137; the fixed one
# finishes in about a millisecond. Nothing lands near the boundary. Proved by
# running this block against the unpatched tree: FAIL, twice out of two.
respawn "$T/parked.err" 8000
# Four connections that each complete ONE request, read the answer, and then go
# quiet -- which is precisely what leaves all four workers parked in the next
# read. The ready-file is written after they are, so the signal below never
# races the setup.
rm -f "$T/parked.ready"
python3 - "$(port_of "$T/parked.err")" "$T/parked.ready" >"$T/parked.out" 2>&1 <<'PY' &
import socket, sys, time
port, ready = int(sys.argv[1]), sys.argv[2]
cs = []
for _ in range(4):
    s = socket.create_connection(("127.0.0.1", port), 3.0)
    s.settimeout(3.0)
    s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n")
    s.recv(65536)          # the answer -- the worker is now back at the top of its loop
    cs.append(s)
time.sleep(0.2)            # ...and now inside the next read_request_capped
open(ready, "w").close()
time.sleep(60)             # hold them open; the shell kills this
PY
PARK=$!
i=0
while [ "$i" -lt 250 ]; do
    [ -f "$T/parked.ready" ] && break
    i=$((i + 1)); sleep 0.02
done
# THE BOUND IS PLATFORM-DEPENDENT, and the difference is the assertion.
# POSIX: shutdown(fd, SHUT_RDWR) on a registered connection wakes the thread
# parked in recv(2) at once, so 3s is a CLIFF -- the pre-batch-A behaviour
# waited out SO_RCVTIMEO and misses it by orders of magnitude.
# WINDOWS: it does not. A thread blocked in recv on a connected socket is not
# released by shutdown() there, so the wind-down costs one idle timeout (8s
# here) and a 3s watchdog would fire on CORRECT behaviour. closesocket() would
# release it -- it is what the listener already gets -- but a connection fd is
# churned by its worker, and closing one hands the number back out while
# another thread may still be blocked on it: the hazard signal_shim.c's
# registry header rejects with measurements. So the Windows bound is above the
# idle timeout, and what it asserts is "it exits 0 without the watchdog", not
# "it exits 0 promptly". Recorded as a behavioural difference in SECURITY.md.
if [ "$IS_WINDOWS" = 1 ]; then PARKWD=15; else PARKWD=3; fi
( sleep "$PARKWD"; srv_kill ) >/dev/null 2>&1 &
WD=$!
srv_sig TERM
wait "$SRV" 2>/dev/null
rc=$?
kill "$WD" 2>/dev/null
wait "$WD" 2>/dev/null
kill "$PARK" 2>/dev/null
wait "$PARK" 2>/dev/null
SRV=""
if [ "$rc" -eq 0 ] && grep -q '^tycho-httpd: stopped after [0-9]' "$T/parked.err"; then
    echo "  ok   SIGTERM with 4 parked keep-alive readers: exit 0 inside the ${PARKWD}s watchdog (idle is 8s)"
else
    echo "  FAIL SIGTERM with parked readers: wait status $rc, want 0 (watchdog was ${PARKWD}s)"; fail=1
    tail -n 3 "$T/parked.err" | sed 's/^/      /'
fi


# ---- the command line -------------------------------------------------------
"$HTTPD" --help >"$T/help.out" 2>&1
rc=$?
if [ "$rc" -eq 0 ] && grep -q '^  --port N ' "$T/help.out"; then
    echo "  ok   --help: exit 0, documents --port"
else
    echo "  FAIL --help: exit $rc"; sed 's/^/      /' "$T/help.out"; fail=1
fi
"$HTTPD" --bogus >"$T/bogus.out" 2>&1
rc=$?
if [ "$rc" -eq 1 ] && grep -q '^tycho-httpd: unknown option: --bogus$' "$T/bogus.out"; then
    echo "  ok   --bogus: exit 1, names the option"
else
    echo "  FAIL --bogus: exit $rc"; sed 's/^/      /' "$T/bogus.out"; fail=1
fi
"$HTTPD" --port 70000 >"$T/range.out" 2>&1
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
