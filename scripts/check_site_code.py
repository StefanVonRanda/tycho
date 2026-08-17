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

TWO LEGS, and the second is the one that matters:
  [1] every Tycho listing COMPILES.
  [2] every listing whose last line carries a `# <output>` comment is RUN, and
      its stdout must equal that comment. A listing can compile perfectly and
      still print something other than the answer printed beside it, which is a
      subtler lie than a syntax error and exactly as damaging.

Leg [1] alone would have caught the range() defect. Leg [2] is what stops the
next one, where the code is valid and the promised output is stale.

Listings are extracted by stripping the syntax-highlight <span>s. The page wraps
digits in spans, which is why a plain `grep 'range('` over the HTML found
nothing while the rendered text plainly said `range(1, 6)` -- do not "simplify"
this back to a regex over the raw markup.

Shell/terminal blocks are skipped by design: they name a release URL and a
`git clone`, and neither is this gate's business.
"""
import html
import os
import re
import subprocess
import sys
import tempfile

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
        got = bool(check(mutated, tychoc))
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
    print("selfcheck: %s" % ("ok (both legs redden on their own mutation)" if not bad
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
    if fails:
        print("site-code check: FAILED (%d)" % len(fails))
        for f in fails:
            print("  " + f)
        return 1
    n = len(listings(page))
    print("site-code check: ok (%d Tycho listing(s) compile, run, and print what "
          "the page says they print)" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
