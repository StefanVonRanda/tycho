#!/usr/bin/env python3
"""Gate the runtime-embed rule: it must be awk-independent and lossless.

The rule turns runtime/tycho_rt.c into one C string literal. Debian's and
Ubuntu's mawk 1.3.4 20200120 halves a backslash in a gsub replacement, so the
old rule under-escaped all 89 backslashes in the runtime and every fresh clone
on those distros died with "\\x used with no following hex digits". No lane in
this tree could see it: every gate here runs against a header this host's awk
had already written correctly.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNTIME = os.path.join(ROOT, "runtime", "tycho_rt.c")
fails = []


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, **kw)


def recipe():
    """The embed recipe as make itself expands it -- never a second copy of it."""
    os.utime(RUNTIME, None)
    env = dict(os.environ)
    for k in ("MAKEFLAGS", "MAKELEVEL", "MFLAGS"):
        env.pop(k, None)
    out = run(["make", "--no-print-directory", "-n", "build/tycho_rt_embed.h"],
              env=env).stdout.decode()
    lines = [l for l in out.splitlines() if not l.startswith("make[")]
    for i, ln in enumerate(lines):
        if ln.lstrip().startswith("awk "):
            return "\n".join(lines[i:])
    sys.exit("embed-check: no awk recipe in `make -n build/tycho_rt_embed.h`")


def emit(rec, awk, src, dst):
    """Run the recipe with a chosen awk over a chosen source file."""
    body = rec.replace("runtime/tycho_rt.c", src).replace(
        "build/tycho_rt_embed.h", dst)
    n = len(re.findall(r"^awk ", body, re.M))
    assert n == 1, "awk substitution did not apply (%d matches)" % n
    body = re.sub(r"^awk ", awk + " ", body, count=1, flags=re.M)
    r = subprocess.run(["sh", "-c", body], cwd=ROOT, capture_output=True)
    assert r.returncode == 0, r.stderr.decode()
    with open(os.path.join(ROOT, dst), "rb") as f:
        return f.read()


def decode(header):
    """Independent oracle: unescape the C literal back to the source bytes."""
    body = header.decode()
    m = re.match(r'static const char \*TYCHO_RUNTIME =\n(.*)\n;\n\Z', body, re.S)
    if not m:
        return None
    out = []
    for ln in m.group(1).split("\n"):
        if not (ln.startswith('"') and ln.endswith('"')):
            return None
        i, s, cur = 1, ln[:-1], []
        while i < len(s):
            c = s[i]
            if c == "\\":
                i += 1
                if i >= len(s):
                    return None
                nxt = s[i]
                if nxt == "n":
                    cur.append("\n")
                elif nxt in ('\\', '"'):
                    cur.append(nxt)
                else:
                    return None          # a stray escape -- exactly the defect
            elif c == '"':
                return None
            else:
                cur.append(c)
            i += 1
        out.append("".join(cur))
    return "".join(out).encode()


def compiles(header, tmp):
    h = os.path.join(tmp, "e.h")
    c = os.path.join(tmp, "e.c")
    with open(h, "wb") as f:
        f.write(header)
    with open(c, "w") as f:
        f.write('#include "e.h"\nconst char *p(void){return TYCHO_RUNTIME;}\n')
    cc = os.environ.get("CC", "cc")
    return subprocess.run([cc, "-fsyntax-only", "-I", tmp, c],
                          capture_output=True).returncode == 0


def leg(tag, msg):
    """Report a leg by what the fail list holds, never by reaching the print."""
    bad = [f for f in fails if f.startswith(tag)]
    print("%s %s -- %s" % (tag, "FAIL" if bad else "ok", msg))


def main():
    rec = recipe()
    awks = [a for a in ("awk", "gawk", "mawk", "original-awk", "busybox awk")
            if shutil.which(a.split()[0])]
    tmp = tempfile.mkdtemp(prefix="embed-check.")
    src = open(RUNTIME, "rb").read()

    # [1] every awk on this host writes the same bytes, and they round trip.
    digests = {}
    for a in awks:
        b = emit(rec, a, "runtime/tycho_rt.c", "build/embed-check.h")
        digests[a] = hashlib.md5(b).hexdigest()[:8]
        if decode(b) != src:
            fails.append("[1] %s: header does not decode back to the runtime" % a)
        if not compiles(b, tmp):
            fails.append("[1] %s: header does not compile" % a)
    if len(set(digests.values())) != 1:
        fails.append("[1] awks disagree: %r" % digests)
    leg("[1]", "awks: " + ", ".join("%s=%s" % (a, digests[a]) for a in awks))
    if len(awks) < 2:
        print("[1] WARNING: only %d awk on PATH -- independence is untested"
              % len(awks))

    # [2] a synthetic runtime whose every line is an escape hazard.
    syn = os.path.join("build", "embed-check-src.c")
    hazard = (b'char a[]="\\x41";\nchar b[]="\\\\";\nchar c[]="q\\"q";\n'
              b'/* \\ */\nchar d[]="\\n";\n')
    with open(os.path.join(ROOT, syn), "wb") as f:
        f.write(hazard)
    for a in awks:
        if decode(emit(rec, a, syn, "build/embed-check.h")) != hazard:
            fails.append("[2] %s: synthetic hazards do not round trip" % a)
    leg("[2]", "synthetic hazard corpus round trips under %d awk(s)" % len(awks))

    # [3] control: drop the escaping branch and every leg above must redden.
    marker = "out bs c : out c"
    if rec.count(marker) != 1:
        fails.append("[3] control substitution did not apply: the recipe no "
                     "longer contains the escaping branch this control mutates")
    else:
        broken = rec.replace(marker, "out c : out c", 1)
        b = emit(broken, awks[0], "runtime/tycho_rt.c", "build/embed-check.h")
        if decode(b) == src:
            fails.append("[3] CONTROL DEAD: unescaped header still round trips")
        if compiles(b, tmp):
            fails.append("[3] CONTROL DEAD: unescaped header still compiles")
        if decode(emit(rec, awks[0], "runtime/tycho_rt.c",
                       "build/embed-check.h")) != src:
            fails.append("[3] revert did not restore the rule")
    leg("[3]", "control: unescaped rule rejected by both legs; revert clean")

    shutil.rmtree(tmp, ignore_errors=True)
    for p in ("build/embed-check.h", syn):
        try:
            os.remove(os.path.join(ROOT, p))
        except OSError:
            pass
    if fails:
        print("embed-check: FAILED")
        for f in fails:
            print("  " + f)
        return 1
    print("embed-check: all green (%d awk(s), 3 legs)" % len(awks))
    return 0


if __name__ == "__main__":
    sys.exit(main())
