#!/usr/bin/env python3
"""WCAG contrast for the landing page's palette -- gh-pages:index.html.

WHY THIS LANE EXISTS. The site's colours were hand-tuned twice and both times a
failure shipped. The second pass fixed ten pairs and still missed two, because it
checked the pairs it remembered rather than the pairs the page renders. Nothing
in this tree could redden for a colour: the site is a single HTML file on a
branch with no Makefile, no scripts/ and -- until this commit -- no reachable
hook. `make check-links` cannot see a hex code.

THE PAIR TABLE IS MEASURED, NOT GUESSED. Every row below came from walking the
rendered page in a real browser (each element's colour against its effective
background, borders included, ::before/::after included, both themes) on
2026-08-17. That measurement is what found the two the eye missed, and it found
three dead tokens as well. A hand-written table would have encoded the same
wrong assumption twice: `--code-bg` is DARK IN BOTH THEMES, so the light theme's
accent has to be legible on near-black and near-white at once, which no single
green does. Re-derive with the probe recorded in this file's git history rather
than by reasoning about which tokens "obviously" meet.

TWO LEGS BEYOND THE RATIOS, and they are what stop the table rotting:
  - every token DEFINED in the palette must appear in PAIRS or in EXEMPT with a
    reason, so a new token cannot arrive ungated;
  - every token NAMED here must still exist in the palette, so deleting one does
    not leave a row that passes by never being evaluated.

`--selfcheck` runs the negative controls: a known-bad palette must be caught in
each theme, a known-good one must not be, and each structural leg must redden on
a mutation aimed at it. Without those a green run means nothing.

Not covered: anything that is not a palette token. Four syntax-highlight colours
are written as literal hex in the CSS and are listed in PAIRS explicitly; if a
fifth is added in literal form, the token legs cannot see it.
"""
import re
import subprocess
import sys

# --- WCAG 2.1 relative luminance and contrast ratio (1.4.3 / 1.4.11) ---------


def _srgb(c):
    c /= 255.0
    return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4


def lum(hexstr):
    h = hexstr.lstrip("#")
    if len(h) == 3:
        h = "".join(ch * 2 for ch in h)
    r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * _srgb(r) + 0.7152 * _srgb(g) + 0.0722 * _srgb(b)


def ratio(fg, bg):
    a, b = lum(fg), lum(bg)
    hi, lo = max(a, b), min(a, b)
    return (hi + 0.05) / (lo + 0.05)


# --- the measured pairs -----------------------------------------------------
# (fg, bg, minimum, what it is). A name starting with "--" is a palette token;
# anything else is a literal colour written directly in the CSS.
#
# 4.5 is 1.4.3 (normal-size text). 3.0 is 1.4.11 (borders, rules, UI edges).
# No text on this page is large enough for the 3.0 text exemption -- the
# smallest, .badge, is 8.5px, which is why its row is 4.5 and not 3.0.
PAIRS = [
    ("--ink",       "--paper",   4.5, "body and heading text"),
    ("--ink-soft",  "--paper",   4.5, "secondary prose"),
    ("--dim",       "--paper",   4.5, "link text, nav"),
    ("--faint",     "--paper",   4.5, "section rails, run captions, btn suffixes"),
    ("--acc",       "--paper",   4.5, "accent text on paper (h1 .tok, .cmd, .spec b)"),
    ("--line",      "--paper",   3.0, "hairlines -- the whole layout is made of them"),
    ("--paper",     "--ink",     4.5, "inverted text in the filled CTA block"),
    ("--line",      "--ink",     3.0, "hairline inside the filled CTA block"),
    ("--code-fg",   "--code-bg", 4.5, "code panel body"),
    ("--code-dim",  "--code-bg", 4.5, "code comments and dim spans"),
    ("--acc-code",  "--code-bg", 4.5, ".badge (8.5px) and .s string literals"),
    ("--code-line", "--code-bg", 3.0, "code panel rules"),
    ("--line",      "--code-bg", 3.0, "panel outer edge"),
    ("--acc-on",    "--acc",     4.5, "nav CTA on hover: text on the accent fill"),
    # Literal syntax colours. Not tokens, so the structural legs are blind to
    # them; listed here so at least these four are scored.
    ("#ffffff",     "--code-bg", 4.5, "syntax .k keyword"),
    ("#cfd4dc",     "--code-bg", 4.5, "syntax .fn function"),
    ("#9aa1ab",     "--code-bg", 4.5, "syntax .g generic"),
    ("#8b929c",     "--code-bg", 4.5, "syntax .out program output"),
]

# Tokens that are deliberately not scored. A token is only allowed here with a
# reason -- "unused" is not one of them, that is a token to delete.
EXEMPT = {
    "--dot": "decorative dot-matrix field behind the hero, and the 6px terminal "
             "chrome dots. Both carry no information and convey no state, so "
             "1.4.11 does not apply. Measured 2026-08-17: no text ever sits on "
             "either -- if that changes this exemption is wrong.",
}


def palette(text):
    """Return {theme: {token: hex}} for the two :root blocks."""
    out = {}
    for theme, pat in (("dark", r":root\s*\{([^}]*)\}"),
                       ("light", r':root\[data-theme="light"\]\s*\{([^}]*)\}')):
        m = re.search(pat, text)
        if not m:
            continue
        out[theme] = {k: v.lower() for k, v in
                      re.findall(r"(--[a-z0-9-]+)\s*:\s*(#[0-9a-fA-F]{3,6})\s*;", m.group(1))}
    # The light block inherits every token it does not restate.
    if "dark" in out and "light" in out:
        for k, v in out["dark"].items():
            out["light"].setdefault(k, v)
    return out


def check(text, label="index.html"):
    """Return a list of failure strings. Empty means clean."""
    pal = palette(text)
    fails = []
    if set(pal) != {"dark", "light"}:
        return ["could not find both :root palettes (found: %s)" % (sorted(pal) or "none")]

    named = {n for row in PAIRS for n in row[:2] if n.startswith("--")} | set(EXEMPT)
    defined = set(pal["dark"])

    for tok in sorted(defined - named):
        fails.append("token %s is defined but scored by no pair and has no EXEMPT "
                     "reason -- add a pair, or delete the token" % tok)
    for tok in sorted(named - defined):
        fails.append("%s is named here but no longer defined in the palette -- its "
                     "row would pass by never being evaluated" % tok)
    if fails:
        return fails  # resolving names below would raise on the missing ones

    for theme in ("dark", "light"):
        for fg, bg, need, why in PAIRS:
            f = pal[theme][fg] if fg.startswith("--") else fg
            b = pal[theme][bg] if bg.startswith("--") else bg
            r = ratio(f, b)
            if r + 5e-3 < need:
                fails.append("%s %-11s %-10s on %-10s %s on %s = %.2f, need %.1f  (%s)"
                             % (theme, fg, "", bg, f, b, r, need, why))
    return fails


# --- negative controls ------------------------------------------------------

def selfcheck(argv=()):
    """Each leg must be shown able to redden. A gate that cannot fail is decoration."""
    src = read_site(argv)
    if src is None:
        print("selfcheck: SKIPPED (no site source to mutate)")
        return 0
    bad = 0

    def expect(name, text, want_fail):
        nonlocal bad
        got = bool(check(text))
        ok = got == want_fail
        print("  %-58s %s" % (name, "ok" if ok else "BROKEN"))
        if not ok:
            bad += 1

    expect("[c0] the real palette is clean", src, False)
    # [c1]/[c2]: a failing colour must be caught in EACH theme independently. The
    # 2026-08-16 pass fixed the dark theme first and the light one only because
    # both were checked; one-theme scoring would have shipped half a fix.
    for theme, pat in (("dark", r"(:root\s*\{[^}]*?--line:)#[0-9a-f]{6}"),
                       ("light", r'(:root\[data-theme="light"\]\s*\{[^}]*?--line:)#[0-9a-f]{6}')):
        mutated, n = re.subn(pat, r"\g<1>#232326", src, count=1, flags=re.S)
        if n != 1:
            print("  [c%s] could not mutate --line in the %s palette" % (theme, theme))
            bad += 1
        else:
            expect("[c1/%s] --line pushed back to the old #232326 is caught" % theme,
                   mutated, True)
    # [c3]: a NEW token with no pair and no exemption must redden, or the table
    # silently stops covering the palette as it grows.
    expect("[c3] an unscored new token is caught",
           re.sub(r"(:root\s*\{)", r"\1--newthing:#123456;", src, count=1), True)
    # [c4]: deleting a token named in PAIRS must redden rather than leaving a row
    # that passes because it is never evaluated.
    expect("[c4] a token deleted from under a pair is caught",
           re.sub(r"--code-dim:#[0-9a-f]{6};\s*", "", src, count=1), True)
    # [c5]: the mutation used by [c1] must NOT trip anything else, or [c1] proves
    # only that some check somewhere fired.
    expect("[c5] an unrelated legal colour change stays clean",
           re.sub(r"(--ink:)#f4f3ef", r"\1#ffffff", src, count=1), False)

    print("selfcheck: %s" % ("ok (every leg reddens on its own mutation)" if not bad
                             else "FAILED (%d leg(s) do not behave)" % bad))
    return bad


def read_site(argv=()):
    """The page source: `--rev <sha>`, else a path, else the published branch.

    The hook passes --rev with the sha being PUSHED. A branch name would be the
    wrong subject: the local `gh-pages` ref in this checkout sat 30 commits
    behind origin while the site was being rewritten in a worktree, so a gate
    keyed on it would have scored a design that has not been live for a week.
    origin/ comes first for the same reason -- for a manual run, "what is
    published" is the useful question.
    """
    argv = list(argv)
    for i, a in enumerate(argv):
        if a == "--rev" and i + 1 < len(argv):
            return _show(argv[i + 1]) or _die("no index.html at rev %s" % argv[i + 1])
        if not a.startswith("-"):
            return open(a, encoding="utf-8").read()
    for ref in ("origin/gh-pages", "gh-pages"):
        src = _show(ref)
        if src is not None:
            return src
    return None


def _show(ref):
    try:
        return subprocess.run(["git", "show", "%s:index.html" % ref],
                              capture_output=True, check=True).stdout.decode("utf-8")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def _die(msg):
    sys.exit("contrast check: FAILED (%s)" % msg)


def main():
    argv = sys.argv[1:]
    if "--selfcheck" in argv:
        return 1 if selfcheck(argv) else 0
    src = read_site(argv)
    if src is None:
        # Skip loudly rather than block a push in a clone with no site branch.
        print("contrast check: SKIPPED (no gh-pages:index.html in this repository)")
        return 0
    fails = check(src)
    if fails:
        print("contrast check: FAILED (%d)" % len(fails))
        for f in fails:
            print("  " + f)
        print("  WCAG 2.1: 4.5:1 for text under 24px (1.4.3), 3:1 for rules and UI "
              "edges (1.4.11).")
        return 1
    n = len(PAIRS) * 2
    print("contrast check: ok (%d pairs across both themes, every palette token "
          "scored or exempt)" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
