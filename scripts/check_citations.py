#!/usr/bin/env python3
"""Citation gate: every `path:N` reference in the tree must name a file that
exists and a line range inside it; every anchored ref must name a token that
occurs on exactly one line of that range.

Three directions, all checked in one pass:

  DOC  -> anything      `path:N` / `path@SYMBOL` in a Markdown file.
  SRC  -> SOURCE        `path:N` in a non-Markdown source file, filtered
                         against the tracked-file set.
  SRC  -> DOC           a `docs/` path in a non-Markdown source file.

The rules, in brief:

  * A bare `:N` inherits the path named on the same line. A bare ref inheriting
    a `.md` path from an earlier line is a failure -- a number that lands
    inside a document is checked by nothing. A `> Provenance:` ref that
    inherits no path is a failure, and a single-line ref inside a
    `> Provenance:` block must carry `@token`, with the token on that line.
  * An anchored `path:N@token` must contain `token` on exactly one line of the
    range (a token on several lines names none of them). An absolute path is a
    failure: write it repo-relative.
  * `path@SYMBOL` names a definition and carries no line: checked only that
    the symbol still appears somewhere in the file. It never becomes the
    inherited subject of a following bare `:N`.
  * `compiler/tychoc0.ty` is exempt from being checked: frozen and unfixable.
  * A dotted-decimal `127.0.0.1:8080` is not a citation and never overwrites
    the paragraph's inherited path.
"""

import re
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# WHAT A MARKDOWN CITATION IS ALLOWED TO NAME. Every tracked source tree plus
# the top-level documents. `plan.md` is excluded on purpose: its phases cite
# live work and rotate.
SRC_PREFIX = ("docs/", "src/", "compiler/", "runtime/", "corelib/", "tests/",
              "scripts/", "tools/", "examples/", "server/", "bench/", "fuzz/",
              "editors/", ".githooks/",
              "docs/internals/FRICTION.md", "README.md", "ROADMAP.md", "CLAUDE.md",
              "CONTRIBUTING.md", "RELEASE_NOTES.md", "SECURITY.md")

# Source trees scanned for the SRC -> DOC direction.
DOC_SCAN_PREFIX = ("src/", "compiler/", "runtime/", "corelib/", "tests/",
                   "scripts/", "tools/", "examples/", "bench/", "fuzz/",
                   "server/", "editors/", ".githooks/", "Makefile")

# Files whose own citations are not policed: frozen and unfixable.
SKIP_CITER = ("compiler/tychoc0.ty",)
SKIP_SUFFIX = (".err", ".out")

# A host and a port is not a path and a line: every dot-separated component of
# `127.0.0.1` is all digits, which no tracked filename is.
HOSTPORT = re.compile(r'^\d+(?:\.\d+)+$')

# `path:N`, `path:N-M`, `path:N-M@token` in Markdown (backticked).
CITE = re.compile(r'`(?:([A-Za-z0-9_./-]+\.[A-Za-z0-9]+))?:(\d+)(?:-(\d+))?'
                  r'(?:@([^`]+))?`')

# `path@SYMBOL` in Markdown: a citation to a definition, deliberately without
# a line number.
SYMCITE = re.compile(r'`((?:[A-Za-z0-9_./-]+\.[A-Za-z0-9]+)|Makefile)@([A-Za-z0-9_]+)`')

# The same form in a source file, filtered against the tracked set.
SYMCITE_SRC = re.compile(r'\b((?:[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]+)'
                         r'|Makefile)@([A-Za-z0-9_]+)')

# A source file naming a document: `docs/<path>.md`, optionally `:N` or `:N-M`.
DOCCITE = re.compile(r'(docs/[A-Za-z0-9_./-]*\.md)(?::(\d+)(?:-(\d+))?)?')

# A source file naming another source file (the third direction). The `:N` is
# MANDATORY here -- a bare `tests/run.sh` is a mention, not a citation -- and
# the match is filtered against the tracked-file set below.
SRCCITE = re.compile(r'((?:[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]+)|Makefile)'
                     r':(\d+)(?:-(\d+))?(?:@([A-Za-z0-9_]+))?')

_cache = {}


def lines_of(path):
    """-> list of lines, or None if the file does not exist"""
    if path not in _cache:
        fp = os.path.join(ROOT, path)
        _cache[path] = (open(fp, errors="replace").read().split("\n")
                        if os.path.exists(fp) else None)
    return _cache[path]


def anchored_lines(src, a, b, tok):
    """-> the line numbers in [a,b] that literally contain `tok`."""
    return [i for i in range(a, b + 1) if tok in src[i - 1]]


def where_at(on):
    """-> ':3, :4, :12, :35, ...' -- elided, not silently truncated."""
    return ", ".join(":%d" % i for i in on[:4]) + (", ..." if len(on) > 4 else "")


def main():
    # Filter in Python rather than passing `*.md` as a pathspec. Handing a
    # wildcard to git THROUGH subprocess loses it on Windows: measured on
    # Windows 11 26200, `git ls-files '*.md'` from the MSYS2 shell answers 126
    # files, and the identical argv through subprocess answers 8 -- the
    # top-level ones. The checker then reported `ok` over 8 anchored and 6 bare
    # citations instead of 134 and 812, which is the dangerous failure: a gate
    # that passes because it checked almost nothing. Same class as the
    # posixpath bug in check_goldens.py (b115336).
    mds = [f for f in subprocess.run(["git", "ls-files"], cwd=ROOT,
                                     capture_output=True, text=True,
                                     check=True).stdout.split()
           if f.endswith(".md")]
    fails, n_bare, n_anchored, n_prov = [], 0, 0, 0
    n_bare_prov = 0
    n_sym, n_sym_src = 0, 0
    for md in mds:
        cur = None
        cur_ln = 0          # the line `cur` was named on (see the header)
        prov = False
        for ln, line in enumerate(open(os.path.join(ROOT, md), errors="replace"), 1):
            # A bare `:N` inherits the last path named in the SAME paragraph.
            if not line.strip():
                cur = None
            # A `> Provenance:` BLOCK: the opening line plus every `>` line
            # that continues it. Anything that is not a blockquote line closes
            # it.
            stripped = line.lstrip()
            if stripped.startswith("> Provenance:"):
                prov = True
            elif not stripped.startswith(">"):
                prov = False
            # A CITATION TO A DEFINITION: `path@SYMBOL`. Names no line, so it
            # is checked for one thing only -- the symbol is still spelled that
            # way in that file -- and it deliberately does NOT set `cur`.
            for m in SYMCITE.finditer(line):
                sp, sym = m.group(1), m.group(2)
                if not (sp.startswith(SRC_PREFIX) or sp == "Makefile"):
                    continue
                n_sym += 1
                ssrc = lines_of(sp)
                where = "%s:%d  `%s`" % (md, ln, m.group(0).strip("`"))
                if ssrc is None:
                    fails.append("%s -> %s: NO SUCH FILE" % (where, sp))
                elif not any(sym in l for l in ssrc):
                    fails.append(
                        "%s -> '%s' does not appear anywhere in %s. A symbol "
                        "citation survives insertions but not a RENAME or a "
                        "DELETION, which is the whole of what it promises."
                        % (where, sym, sp))
            for m in CITE.finditer(line):
                if m.group(1):
                    # A HOST AND A PORT IS NOT A CITATION. Skipped WITHOUT
                    # touching `cur`: the span is ignored for want of a
                    # SRC_PREFIX, but must not overwrite the paragraph's real
                    # path for a following bare `:N`.
                    if HOSTPORT.match(m.group(1)):
                        continue
                    cur = m.group(1)
                    cur_ln = ln
                    # AN ABSOLUTE PATH IS A FAILURE, NOT A SKIP. Write it
                    # repo-relative, or this gate cannot check it.
                    if cur.startswith("/"):
                        rel = (cur[len(ROOT) + 1:] if cur.startswith(ROOT + "/")
                               else None)
                        tail = m.group(0).strip("`").split(":", 1)[1]
                        fails.append(
                            "%s:%d  `%s` -> ABSOLUTE PATH, which this gate "
                            "cannot check: %s"
                            % (md, ln, m.group(0).strip("`"),
                               "write it repo-relative, as `%s:%s`" % (rel, tail)
                               if rel else
                               "it names no file inside this repository"))
                        # Never let a following bare `:N` inherit an absolute
                        # path: it would be as unchecked as the one above.
                        cur = None
                        continue
                if not cur:
                    # A `> Provenance:` ref that inherits NO path is not merely
                    # un-anchored, it is entirely unchecked -- no file, no
                    # bounds. Fail closed here; elsewhere a pathless `:N` stays
                    # a deliberate fail-open skip.
                    if prov:
                        fails.append(
                            "%s:%d  `%s` -> a `> Provenance:` ref that names no "
                            "path and inherits none from its paragraph; nothing "
                            "about it is checked. Write the path: "
                            "`<path>:%s`" % (md, ln, m.group(0).strip("`"),
                                             m.group(0).strip("`").lstrip(":")))
                    continue
                if not cur.startswith(SRC_PREFIX):
                    continue
                # A DOCUMENT PATH IS INHERITED ONLY ALONG ITS OWN LINE. A bare
                # `:N` that inherits a `.md` path from an earlier line is
                # checked by nothing -- a number that lands inside a document
                # has no meaning there -- so it is a failure. Write it:
                # `docs/<file>.md:<line>`.
                if (m.group(1) is None and cur.endswith(".md")
                        and cur_ln != ln):
                    fails.append(
                        "%s:%d  `%s` -> a bare ref inheriting the document path "
                        "`%s` from line %d. A number that lands inside a document "
                        "is checked by nothing, so a `docs/` path carries only "
                        "along the line that names it. Write it: `%s%s`"
                        % (md, ln, m.group(0).strip("`"), cur, cur_ln,
                           cur, m.group(0).strip("`")))
                    continue
                a = int(m.group(2))
                b = int(m.group(3)) if m.group(3) else a
                anchor = m.group(4)
                src = lines_of(cur)
                where = "%s:%d  `%s`" % (md, ln, m.group(0).strip("`"))
                if src is None:
                    fails.append("%s -> %s: NO SUCH FILE" % (where, cur))
                    continue
                if a < 1 or b < a or b > len(src):
                    fails.append("%s -> %s has %d lines: OUT OF BOUNDS"
                                 % (where, cur, len(src)))
                    continue
                if anchor is None:
                    n_bare += 1
                    # THE ONE MANDATORY ANCHOR. A single-line ref inside a
                    # `> Provenance:` block must carry `@token`; a RANGE must
                    # not be forced to, so it is deliberately not checked here.
                    if prov and b == a:
                        fails.append(
                            "%s -> un-anchored single-line ref in a `> Provenance:` "
                            "block; write `%s:%d@<token>` with a token that appears "
                            "on that line. It currently reads: %s"
                            % (where, cur, a, src[a - 1].strip()[:70] or "(blank)"))
                    elif prov:
                        n_bare_prov += 1
                    continue
                n_anchored += 1
                if prov and b == a:
                    n_prov += 1
                on = anchored_lines(src, a, b, anchor)
                if not on:
                    hit = [i for i, l in enumerate(src, 1) if anchor in l][:3]
                    fails.append(
                        "%s -> lines %d-%d of %s do NOT contain '%s'%s"
                        % (where, a, b, cur, anchor,
                           ("; it appears at :" + ", :".join(map(str, hit)))
                           if hit else " (token absent from the whole file)"))
                    continue
                # AN AMBIGUOUS ANCHOR IS A FAILURE: a token on more than one
                # line of the range names no line, so the range can drift
                # inside itself and still pass.
                if len(on) > 1:
                    fails.append(
                        "%s -> AMBIGUOUS ANCHOR: '%s' is on %d lines of %s (%s), "
                        "so it names none of them and a drift inside the range "
                        "still passes. Anchor a token that occurs once, tighten "
                        "the range to its construct, or drop the anchor -- a "
                        "range with no single subject token is honestly bare."
                        % (where, anchor, len(on), cur, where_at(on)))
    # --- the second and third directions: SOURCE -> DOC and SOURCE -> SOURCE --
    srcs = subprocess.run(["git", "ls-files"], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout.split()
    tracked = set(srcs)
    n_doc, n_src, n_src_anch = 0, 0, 0
    for sf in srcs:
        if sf.endswith(".md") or not sf.startswith(DOC_SCAN_PREFIX):
            continue
        if sf in SKIP_CITER:
            continue
        try:
            text = open(os.path.join(ROOT, sf), errors="replace").readlines()
        except (IsADirectoryError, OSError):
            continue
        # SOURCE -> SOURCE is policed everywhere except the frozen compiler and
        # golden/error transcript files, which are not hand-edited.
        cites_src = not (sf in SKIP_CITER or sf.endswith(SKIP_SUFFIX))
        for ln, line in enumerate(text, 1):
            if cites_src:
                for m in SYMCITE_SRC.finditer(line):
                    sp, sym = m.group(1), m.group(2)
                    if sp.endswith(".md") or sp not in tracked:
                        continue
                    n_sym_src += 1
                    ssrc = lines_of(sp)
                    if ssrc is not None and not any(sym in l for l in ssrc):
                        fails.append(
                            "%s:%d  `%s` -> '%s' does not appear anywhere in "
                            "%s. A symbol citation survives insertions but not "
                            "a RENAME or a DELETION, which is the whole of what "
                            "it promises."
                            % (sf, ln, m.group(0), sym, sp))
                for m in SRCCITE.finditer(line):
                    tgt = m.group(1)
                    if tgt.endswith(".md") or tgt not in tracked:
                        continue
                    sl = lines_of(tgt)
                    a = int(m.group(2))
                    b = int(m.group(3)) if m.group(3) else a
                    if a < 1 or b < a or b > len(sl):
                        n_src += 1
                        fails.append("%s:%d  `%s` -> %s has %d lines: OUT OF BOUNDS"
                                     % (sf, ln, m.group(0), tgt, len(sl)))
                        continue
                    if m.group(4) is None:
                        n_src += 1
                        continue
                    # ANCHORED source -> source (opt-in): the cited lines must
                    # literally contain the token.
                    n_src_anch += 1
                    on = anchored_lines(sl, a, b, m.group(4))
                    if not on:
                        hit = [i for i, l in enumerate(sl, 1) if m.group(4) in l][:3]
                        fails.append(
                            "%s:%d  `%s` -> lines %d-%d of %s do NOT contain '%s'%s"
                            % (sf, ln, m.group(0), a, b, tgt, m.group(4),
                               ("; it appears at :" + ", :".join(map(str, hit)))
                               if hit else " (token absent from the whole file)"))
                        continue
                    # AN AMBIGUOUS ANCHOR IS A FAILURE, same rule as the
                    # Markdown pass.
                    if len(on) > 1:
                        fails.append(
                            "%s:%d  `%s` -> AMBIGUOUS ANCHOR: '%s' is on %d lines "
                            "of %s (%s), so it names none of them and a drift "
                            "inside the range still passes. Anchor a token that "
                            "occurs once, tighten the range to its construct, or "
                            "drop the anchor -- a range with no single subject "
                            "token is honestly bare."
                            % (sf, ln, m.group(0), m.group(4), len(on), tgt,
                               where_at(on)))
            for m in DOCCITE.finditer(line):
                doc = m.group(1)
                n_doc += 1
                where = "%s:%d  `%s`" % (sf, ln, m.group(0))
                dl = lines_of(doc)
                if dl is None:
                    fails.append("%s -> %s: NO SUCH DOCUMENT" % (where, doc))
                    continue
                if m.group(2):
                    a = int(m.group(2))
                    b = int(m.group(3)) if m.group(3) else a
                    if a < 1 or b < a or b > len(dl):
                        fails.append("%s -> %s has %d lines: OUT OF BOUNDS"
                                     % (where, doc, len(dl)))
    if "--stats" in sys.argv:
        print("citation check: %d anchored (content-checked, %d of them the "
              "mandatory `> Provenance:` single-line refs), %d bare (bounds "
              "only, %d exempt `> Provenance:` ranges), %d source->doc "
              "(existence), %d source->source (bounds), %d source->source "
              "anchored (content-checked), %d `path@SYMBOL` definition refs "
              "(%d of them from source)"
              % (n_anchored, n_prov, n_bare, n_bare_prov, n_doc, n_src,
                 n_src_anch, n_sym + n_sym_src, n_sym_src))
    if fails:
        for f in fails:
            print("STALE  " + f)
        print("citation check: FAILED (%d stale citation(s) above)" % len(fails))
        return 1
    print("citation check: ok (%d anchored contain the token they name and each "
          "names one line, %d bare in bounds, %d source->doc citations resolve, "
          "%d source->source in bounds, %d source->source anchored, "
          "%d `path@SYMBOL` definition refs name a symbol still in their file)"
          % (n_anchored, n_bare, n_doc, n_src, n_src_anch, n_sym + n_sym_src))
    return 0


if __name__ == "__main__":
    sys.exit(main())
