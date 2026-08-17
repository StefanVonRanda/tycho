#!/usr/bin/env python3
"""Every Tycho listing on the landing page must compile -- and print what it claims.

WHY THIS LANE EXISTS. On 2026-08-17 the first code a visitor saw on
tycho-lang.com used `range(1, 6)`, which the compiler removed. It had been live
for an unknown length of time. Nothing in this tree could redden for it:
`make docs-fences` compiles fences under `docs/`, and the site is a single HTML
file on the `gh-pages` branch, outside that lane's reach -- the same blind spot
that let ten WCAG failures ship, one branch over. Contrast, links and citation
gates all treat a code block as TEXT.

It was found by accident: a scratch file for an unrelated experiment was copied
from the page, and the compiler rejected it. Nothing on the site had ever
executed its own code.

THE LEGS, and the later ones are the ones that matter:
  [1] every Tycho listing COMPILES.
  [2] every listing whose last line carries a `# <output>` comment is RUN, and
      its stdout must equal that comment. A listing can compile perfectly and
      still print something other than the answer printed beside it, which is a
      subtler lie than a syntax error and exactly as damaging.
  [3a] every release URL names the LATEST published tag. This is how the page
      rots with nobody touching it: 0.8 ships and the download block still
      offers 0.7.
  [3b] the install transcripts are DRIVEN on a real terminal and must match
      what the page shows, character for character.

Leg [1] alone would have caught the range() defect. Leg [2] is what stops the
next one, where the code is valid and the promised output is stale. Leg [3] was
added on 2026-08-17 and immediately found one: both blocks omitted `built
examples/hello`, so a visitor's first command printed a line the page had not
predicted.

Listings are extracted by stripping the syntax-highlight <span>s. The page wraps
digits in spans, which is why a plain `grep 'range('` over the HTML found
nothing while the rendered text plainly said `range(1, 6)` -- do not "simplify"
this back to a regex over the raw markup.

A pty is what makes [3b] possible: `what is your name: Ada` is the terminal
echoing what was TYPED, and a captured pipe prints `what is your name: hello
Ada` on one line instead. A transcript claims a session, so it is checked in a
session. This paragraph replaced one saying shell blocks were "skipped by
design ... neither is this gate's business", written before the defect above
was found in exactly the blocks it excused.
"""
import html
import os
import re
import select
import subprocess
import sys
import tempfile
import time

TYCHO_START = re.compile(r"^\s*(package|fn|struct|enum|import)\s", re.M)


def listings(page):
    """Return [(index, source)] for each Tycho code block in the page."""
    out = []
    for i, block in enumerate(
            re.findall(r"<pre[^>]*>\s*<code[^>]*>(.*?)</code>\s*</pre>", page, re.S)):
        src = html.unescape(re.sub(r"<[^>]+>", "", block))
        if TYCHO_START.search(src):
            out.append((i, src))
    return out


def claimed_output(src):
    """The `# ...` comment on the last non-empty line, if it looks like output."""
    for line in reversed(src.strip().splitlines()):
        if not line.strip():
            continue
        m = re.search(r"#\s*(\S.*?)\s*$", line)
        return m.group(1) if m else None
    return None


def check(page, tychoc):
    fails = []
    found = listings(page)
    if not found:
        return ["no Tycho listing found on the page -- the extractor is broken, or "
                "the page lost its code (either way this gate is scoring nothing)"]

    with tempfile.TemporaryDirectory() as tmp:
        for idx, src in found:
            d = os.path.join(tmp, "b%d" % idx)
            os.makedirs(d)
            body = src if src.lstrip().startswith("package") else "package main\n" + src
            path = os.path.join(d, "m.ty")
            with open(path, "w", encoding="utf-8") as f:
                f.write(body)

            # [1] compile
            exe = os.path.join(d, "prog")
            r = subprocess.run([tychoc, path, "-o", exe],
                               capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(exe):
                first = (r.stderr or r.stdout).strip().splitlines()
                fails.append("listing %d does not compile: %s"
                             % (idx, first[0] if first else "no diagnostic"))
                continue

            # [2] run, and compare against the output printed beside it
            want = claimed_output(src)
            if want is None:
                continue
            try:
                r = subprocess.run([exe], capture_output=True, text=True, timeout=10)
            except subprocess.TimeoutExpired:
                fails.append("listing %d did not terminate in 10s" % idx)
                continue
            got = r.stdout.strip()
            if want not in got.splitlines() and want != got:
                fails.append("listing %d prints %r but the page claims %r"
                             % (idx, got, want))
    return fails


def terminals(page):
    """Return the text of each <pre> block that is a shell transcript."""
    out = []
    for block in re.findall(r"<pre[^>]*>(.*?)</pre>", page, re.S):
        t = html.unescape(re.sub(r"<[^>]+>", "", block)).strip()
        if re.search(r"^\s*\$ ", t, re.M):
            out.append(t)
    return out


def drive(cmd, expected, cwd, timeout=120):
    """Run `cmd` on a real terminal and return what the terminal showed.

    The transcript claims a SESSION, not a pipe: `what is your name: Ada` is the
    tty echoing what was typed, and a captured pipe never contains it. So this
    runs under a pty and uses the transcript itself as the script -- whenever
    the program stops with what it has printed being a strict prefix of what the
    page promises, the rest of that promised line is typed in. If the output
    diverges, nothing is typed and the comparison fails, which is the point.
    """
    pid, fd = os.forkpty()
    if pid == 0:                                    # child: never returns
        os.chdir(cwd)
        os.execv("/bin/sh", ["sh", "-c", cmd])
    buf, t0 = "", time.time()
    try:
        while time.time() - t0 < timeout:
            r, _, _ = select.select([fd], [], [], 0.4)
            if r:
                try:
                    d = os.read(fd, 4096)
                except OSError:
                    break
                if not d:
                    break
                buf += d.decode("utf-8", "replace").replace("\r\n", "\n")
                continue
            seen = buf.replace("\r", "")
            # type only at a PROMPT -- output so far is a strict prefix of what
            # the page promises AND does not end in a newline. Without the
            # second half the first idle moment (the compile) looks like a
            # prompt, and the whole expected transcript is typed into the
            # terminal before the program has printed anything at all.
            if (seen and not seen.endswith("\n")
                    and expected.startswith(seen) and len(seen) < len(expected)):
                rest = expected[len(seen):].split("\n")[0]
                os.write(fd, (rest + "\n").encode())
            elif seen and not expected.startswith(seen):
                break
    finally:
        try:
            os.close(fd)
        except OSError:
            pass
        try:
            os.waitpid(pid, 0)
        except ChildProcessError:
            pass
    return buf.replace("\r", "")


def check_terminals(page, root):
    """[3] the install transcripts: the URLs are current, and the session is real."""
    fails, checked = [], 0
    blocks = terminals(page)
    if not blocks:
        return ["no terminal block found on the page -- the page lost its install "
                "instructions, or this extractor is broken"], 0

    # [3a] a release URL naming a version that is no longer the latest is the
    # way this page rots without anyone touching it. Needs the network; a
    # missing `gh` skips this leg loudly rather than passing it.
    vers = set(re.findall(r"/releases/download/v(\d+\.\d+\.\d+)/", "\n".join(blocks)))
    if vers:
        r = subprocess.run(["gh", "release", "list", "--limit", "1", "--json", "tagName",
                            "--jq", ".[0].tagName"], capture_output=True, text=True,
                           cwd=root)
        tag = r.stdout.strip()
        if r.returncode != 0 or not tag:
            print("  [3a] release version: SKIPPED (no `gh`, or offline)")
        else:
            checked += 1
            if vers != {tag.lstrip("v")}:
                fails.append("the page offers %s but the latest published release is %s"
                             % ("/".join("v" + v for v in sorted(vers)), tag))

    # [3b] the last command of each block is run for real when it is a tychoc
    # invocation, and what the terminal shows must equal what the page shows.
    for bi, t in enumerate(blocks):
        lines = t.split("\n")
        cmd_at = max((i for i, l in enumerate(lines) if l.lstrip().startswith("$ ")),
                     default=None)
        if cmd_at is None:
            continue
        cmd = lines[cmd_at].lstrip()[2:].strip()
        if not cmd.startswith("./tychoc"):
            continue
        want = "\n".join(lines[cmd_at + 1:]).strip()
        got = drive(cmd, want + "\n", root).strip()
        checked += 1
        if got != want:
            fails.append("terminal block %d shows %r after `%s`, but a real terminal "
                         "shows %r" % (bi, want, cmd, got))
    return fails, checked


def read_page(argv):
    for i, a in enumerate(argv):
        if a == "--rev" and i + 1 < len(argv):
            return show(argv[i + 1])
        if not a.startswith("-"):
            return open(a, encoding="utf-8").read()
    for ref in ("origin/gh-pages", "gh-pages"):
        p = show(ref)
        if p is not None:
            return p
    return None


def show(ref):
    try:
        return subprocess.run(["git", "show", "%s:index.html" % ref],
                              capture_output=True, check=True).stdout.decode("utf-8")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def selfcheck(page, tychoc):
    """Each leg must redden on a mutation aimed at it, or it is decoration."""
    bad = 0

    def expect(name, mutated, want_fail):
        nonlocal bad
        got = bool(check(mutated, tychoc) + check_terminals(mutated, os.getcwd())[0])
        ok = got == want_fail
        print("  %-56s %s" % (name, "ok" if ok else "BROKEN"))
        if not ok:
            bad += 1

    expect("[c0] the live listings are clean", page, False)
    # [c1] the defect that motivated this lane must be caught again.
    expect("[c1] a reintroduced range() loop is caught",
           page.replace('for</span> i := 1; i &lt; 6; i += 1:',
                        'for</span> i <span class="k">in</span> range(1, 6):'), True)
    # [c2] a listing that compiles but prints the wrong thing must be caught --
    # this is the leg a compile-only gate does not have.
    expect("[c2] a stale claimed output is caught",
           page.replace("# counted: 1, 2, 3, 4, 5", "# counted: 9, 9, 9"), True)
    # [c3] the way this page rots with nobody touching it: a release ships and
    # the download block still names the version before it.
    expect("[c3] a superseded release version is caught",
           page.replace("v0.7.0", "v0.6.0"), True)
    # [c4]/[c5] the transcript's two failure modes -- a line the program prints
    # that the page does not show, and a line the page shows that it never
    # printed. Only [c4] was a real defect; [c5] is the mirror, and a driver
    # that merely looked for its expected lines somewhere in the output would
    # pass it.
    expect("[c4] a transcript missing a printed line is caught",
           page.replace('<span class="out">built examples/hello</span>\n', ""), True)
    expect("[c5] a transcript inventing an answer is caught",
           page.replace("hello Ada", "hello Bob"), True)
    print("selfcheck: %s" % ("ok (every leg reddens on its own mutation)" if not bad
                             else "FAILED (%d leg(s) do not behave)" % bad))
    return bad


def main():
    argv = sys.argv[1:]
    tychoc = os.environ.get("TYCHOC", "./tychoc")
    if not os.path.exists(tychoc):
        subprocess.run(["make", "-s", "tychoc"], check=False)
    if not os.path.exists(tychoc):
        print("site-code check: SKIPPED (no tychoc to compile with)")
        return 0
    page = read_page(argv)
    if page is None:
        print("site-code check: SKIPPED (no gh-pages:index.html in this repository)")
        return 0
    if "--selfcheck" in argv:
        return 1 if selfcheck(page, tychoc) else 0
    fails = check(page, tychoc)
    tf, nterm = check_terminals(page, os.getcwd())
    fails += tf
    if fails:
        print("site-code check: FAILED (%d)" % len(fails))
        for f in fails:
            print("  " + f)
        return 1
    n = len(listings(page))
    print("site-code check: ok (%d Tycho listing(s) compile, run, and print what "
          "the page says they print; %d terminal-block claim(s) hold)" % (n, nterm))
    return 0


if __name__ == "__main__":
    sys.exit(main())
