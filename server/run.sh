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
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1; SORT=/usr/bin/sort ;; *) IS_WINDOWS=0; SORT=sort ;; esac

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

def raw(msg):
    """The bytes exactly as given -- get() would add a Connection header and
    reorder nothing, but framing tests need the header set to be verbatim."""
    return exchange(msg)

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

eq("301 /emptydir status",   status(get(b"/emptydir")), "HTTP/1.1 301 Moved Permanently")
eq("301 /emptydir Location", header(get(b"/emptydir"), "Location"), "/emptydir/")
eq("404 /emptydir/ (no index.html in it)", status(get(b"/emptydir/")), "HTTP/1.1 404 Not Found")

# ---- 403: the two ways out of the root --------------------------------------
# Sent raw, so the ../ survives to the server. This is the case the README's
# transcript needs `curl --path-as-is` for; a raw socket has no normalizer to
# turn off.
eq("403 traversal /../../etc/passwd", status(get(b"/../../etc/passwd")), "HTTP/1.1 403 Forbidden")
eq("403 traversal /a/../../../etc", status(get(b"/a/../../../etc/passwd")), "HTTP/1.1 403 Forbidden")
eq("403 encoded dots %2e%2e", status(get(b"/%2e%2e/%2e%2e/etc/passwd")), "HTTP/1.1 403 Forbidden")
eq("403 encoded slash ..%2f", status(get(b"/..%2f..%2fetc/passwd")), "HTTP/1.1 403 Forbidden")
eq("403 both encoded %2e%2e%2f", status(get(b"/%2e%2e%2f%2e%2e%2fetc/passwd")), "HTTP/1.1 403 Forbidden")
eq("403 encoded body is not the file",
   b"root:" in get(b"/%2e%2e%2f%2e%2e%2fetc/passwd").split(b"\r\n\r\n", 1)[1], False)
# Decoding ONCE is the rule, so a DOUBLE-encoded traversal must NOT become one:
# %252e decodes to the literal text "%2e", which is a filename, not a dot-dot.
# This is the leg that reddens if someone "fixes" traversal by decoding in a loop.
eq("404 double-encoded is a filename, not traversal",
   status(get(b"/%252e%252e/etc/passwd")), "HTTP/1.1 404 Not Found")
eq("403 hidden segment /.hidden/secret.txt",
   status(get(b"/.hidden/secret.txt")), "HTTP/1.1 403 Forbidden")
eq("403 body is not the file", b"root:" in get(b"/../../etc/passwd").split(b"\r\n\r\n", 1)[1], False)

eq("400 non-numeric Content-Length",
   status(raw(b"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 4x\r\n\r\n")),
   "HTTP/1.1 400 Bad Request")
eq("400 two conflicting Content-Length",
   status(raw(b"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nContent-Length: 44\r\n\r\n")),
   "HTTP/1.1 400 Bad Request")
eq("400 Content-Length with Transfer-Encoding",
   status(raw(b"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 6\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")),
   "HTTP/1.1 400 Bad Request")
# The control for all three: a DUPLICATE Content-Length that AGREES is not a
# conflict, and a lone Transfer-Encoding is not ambiguous -- neither may be
# swept into the 400. Without these, "reject anything with two headers" passes.
eq("405 duplicate Content-Length that agrees is not a conflict",
   status(raw(b"POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n")),
   "HTTP/1.1 405 Method Not Allowed")
eq("405 Transfer-Encoding alone is not ambiguous",
   status(raw(b"POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")),
   "HTTP/1.1 405 Method Not Allowed")

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
eq("400 %0d%0a header injection in path",
   status(exchange(b"GET /a%0d%0aInjected:%20yes HTTP/1.1\r\nHost: t\r\n\r\n")),
   "HTTP/1.1 400 Bad Request")
eq("400 %0a alone in path",
   status(exchange(b"GET /a%0ab HTTP/1.1\r\nHost: t\r\n\r\n")), "HTTP/1.1 400 Bad Request")
# The control: a percent-escape of a PRINTABLE byte is ordinary and must still be
# decoded and looked up, not swept into the 400. Without this, "refuse anything
# with a %" passes every leg above.
eq("404 %20 is a printable byte, not a control byte",
   status(exchange(b"GET /no%20such.txt HTTP/1.1\r\nHost: t\r\n\r\n")), "HTTP/1.1 404 Not Found")
eq("400 Content-Length: -5 (smuggling)",
   status(exchange(b"GET / HTTP/1.1\r\nHost: t\r\nContent-Length: -5\r\n\r\n")),
   "HTTP/1.1 400 Bad Request")

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

stopped=$(grep '^tycho-httpd: stopped after [0-9][0-9]* requests$' "$T/srv.err")
if [ -n "$stopped" ]; then
    echo "  ok   SIGTERM: $stopped"
else
    echo "  FAIL SIGTERM: no 'stopped after N requests' line on stderr"; fail=1
fi

chk() {  # chk <name> <expected-count-test> <actual>
    if [ "$2" = "$3" ]; then echo "  ok   $1"; else echo "  FAIL $1: got '$3', want '$2'"; fail=1; fi
}
workers=$(sed -n 's/^\(w[0-9][0-9]*\) .*/\1/p' "$T/srv.err" | "$SORT" -u | tr '\n' ' ')
chk "access log: every worker served (w1..w4)" "w1 w2 w3 w4 " "$workers"

peers=$(sed -n 's/^w[0-9][0-9]* \([^ ]*\) .*/\1/p' "$T/srv.err" | "$SORT" -u | tr '\n' ' ')
chk "access log: peer address on every line (net.peer_addr)" "127.0.0.1 " "$peers"

for code in 200 206 301 400 403 404 405 408 416 431; do
    n=$(grep -c " $code [0-9][0-9]* [0-9.]*ms" "$T/srv.err")
    if [ "$n" -gt 0 ]; then echo "  ok   access log: $n line(s) with status $code"
    else echo "  FAIL access log: no line with status $code"; fail=1; fi
done
n_served=$(printf '%s\n' "$stopped" | sed -n 's/^tycho-httpd: stopped after \([0-9]*\) requests$/\1/p')
n_logged=$(grep -c '^w[0-9]' "$T/srv.err")
chk "SIGTERM: served count == access log lines" "$n_logged" "${n_served:-none}"

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

respawn "$T/emfile.err" 200
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

respawn "$T/busy.err"
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
