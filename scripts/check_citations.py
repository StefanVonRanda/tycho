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
  * A COMMIT HASH written as a citation -- backticked, or after the word
    `commit`/`commits` -- must resolve to a commit in this repository. A token
    qualifies at 7..12 hex characters with both a digit and an `a-f` letter, so
    md5/sha/FNV-64 runs and decimal measurements are out; a CRC32 digest is 8
    and is not, so write one unbackticked. Skipped loudly on a shallow clone.

`--report` adds an advisory listing of drift-prone refs (un-anchored
single-line refs, and very wide ranges). It changes no verdict: tree-wide
mandatory anchoring is a 290-ref flag day, and "this range points at unrelated
code" is not mechanically decidable at all.
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

# THE SAME SHAPE WITH ANY ANCHOR AT ALL, so that an anchor SYMCITE cannot spell
# is rejected instead of silently skipped -- a malformed anchor used to match
# no pattern here at all, which is worse than a bare ref: it reads as policed
# and was checked by nothing. A `:N` cannot appear before the `@` (the path
# class has no `:`), so this never steals a `path:N@token` ref from CITE, which
# checks that form's anchor literally and may keep taking many words.
SYMCITE_ANY = re.compile(r'`((?:[A-Za-z0-9_./-]+\.[A-Za-z0-9]+)|Makefile)@([^`]*)`')
SYM_OK = re.compile(r'[A-Za-z0-9_]+')

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

# A COMMIT HASH AS THIS TREE WRITES ONE: backticked at 7..12 hex characters,
# or any width after the word `commit`. `hashy` below wants both a digit and an
# `a-f` letter, and md5 (32) / sha256 (64) / FNV-64 (16) are far wider -- but a
# CRC32 digest is 8 and does fit, so write one unbackticked (crc=... in prose)
# or it is read as a citation and reddens here. That is deliberate: a reader
# cannot tell a backticked 8-char digest from a short hash either.
HASH_TICK = re.compile(r'`([0-9a-f]{7,12})`')
HASH_WORD = re.compile(r'\bcommits?\s+((?<![0-9a-zA-Z])[0-9a-f]{7,12}'
                       r'(?:\s*,\s*[0-9a-f]{7,12})*(?![0-9a-zA-Z]))')

# A range this wide is reported, never failed: the widest here really is a
# whole declaration parser.
REPORT_WIDE = 100

_cache = {}


def hashy(tok):
    """-> True for a hash-shaped token: a decimal measurement has no `a-f`
    letter, and an English word has no digit."""
    return (any(c.isdigit() for c in tok)
            and any(c in "abcdef" for c in tok))


def commit_hashes(files, fails):
    """-> number of distinct hashes checked, or -1 if the history is absent."""
    seen = {}
    for f in files:
        if f in SKIP_CITER or f.endswith(SKIP_SUFFIX):
            continue
        fp = os.path.join(ROOT, f)
        if not os.path.isfile(fp):
            continue
        for ln, line in enumerate(open(fp, errors="replace"), 1):
            hits = [(m.group(1), m.group(0)) for m in HASH_TICK.finditer(line)]
            for m in HASH_WORD.finditer(line):
                hits += [(h.strip(), m.group(0).strip())
                         for h in m.group(1).split(",")]
            for tok, raw in hits:
                if hashy(tok):
                    seen.setdefault(tok, []).append((f, ln, raw))
    if not seen:
        return 0
    # A SHALLOW CLONE HAS NO HISTORY, so every hash would look wrong. Skipping
    # loudly beats a gate that reddens for the checkout rather than the tree.
    shallow = subprocess.run(["git", "rev-parse", "--is-shallow-repository"],
                             cwd=ROOT, capture_output=True, text=True)
    if shallow.returncode != 0 or shallow.stdout.strip() != "false":
        return -1
    out = subprocess.run(["git", "cat-file", "--batch-check"], cwd=ROOT,
                         input="\n".join(seen) + "\n",
                         capture_output=True, text=True).stdout.split("\n")
    for tok, verdict in zip(seen, out):
        parts = verdict.split()
        kind = parts[1] if len(parts) > 1 else "missing"
        if kind == "commit":
            continue
        for f, ln, raw in seen[tok]:
            fails.append(
                "%s:%d  %s -> '%s' is not a commit in this repository "
                "(git cat-file says: %s). A hash nobody can resolve cites "
                "nothing, and a wrong-but-plausible one cites the wrong thing "
                "silently. Confirm it with `git cat-file -t <hash>`."
                % (f, ln, raw, tok, kind))
    return len(seen)


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


# ---------------------------------------------------------------------------
# STRICT MODE (`--strict`): anything citation-SHAPED that no parser above claims.
#
# The gate has now failed the same way twice -- a malformed anchor, and a
# multi-range -- and both times the shape matched NO pattern here, so it was
# skipped in silence and read as policed. Each was fixed by naming that one bad
# shape, which cannot fix the third one. This pass inverts the question: find
# every token that LOOKS like a citation, subtract every span a strict parser
# claimed, and report what is left over.
#
# The `tracked` filter is what keeps it usable: a candidate whose path is not a
# file in this repo is not a citation, which drops URLs (`host:8080/x`), times,
# `pkg@version`, and every `foo.c:` that is really prose.
#
# A COMMA IS PART OF THE TOKEN WHEN A DIGIT FOLLOWS IT. Excluding `,` outright
# (the first cut) made the candidate stop at the comma, so a BARE `path:N,M` in
# Markdown looked fully covered by SRCCITE and slipped through the very backstop
# this pass is. Caught by the probe table, not by reading.
CAND = re.compile(
    r'(?<![A-Za-z0-9_./-])'
    r'((?:[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]+)|Makefile)'
    r'([:@])((?:[^\s`)\],;"\']|,(?=\d))*)')


def unclaimed(line, tracked, claimers):
    """Citation-shaped tokens on `line` that no strict parser fully matched."""
    spans = [(m.start(), m.end()) for rx in claimers for m in rx.finditer(line)]
    out = []
    for m in CAND.finditer(line):
        path, sep, rest = m.group(1), m.group(2), m.group(3)
        if path not in tracked:
            continue                 # not a file here -> not a citation
        if sep == ":" and not rest[:1].isdigit():
            continue                 # `path: prose` is a label, not a ref
        if sep == "@" and not rest:
            continue                 # a bare `path@` is caught by SYMCITE_ANY
        # SENTENCE PUNCTUATION IS NOT PART OF THE REF. Without this the first
        # run flagged 36 refs the gate parses perfectly -- a path-and-line at the
        # end of a sentence, or before a quote -- because the candidate swallowed
        # the trailing character and so was not covered by the strict span.
        # (The examples are described rather than written: spelling one here
        # would make this comment a live citation, which is how the count moved
        # by one the first time I wrote it.) An over-greedy detector reporting correct refs as
        # broken is worse than the hole it was written to close.
        end = m.end()
        while end > m.start() and line[end - 1] in ".,:;":
            end -= 1
        if any(a <= m.start() and end <= b for a, b in spans):
            continue                 # a strict parser claimed the whole token
        out.append(line[m.start():end])
    return out

# gap: only the two directions the gate already walks (tracked .md, and source
# files under DOC_SCAN_PREFIX). A citation in an UNTRACKED file is invisible to
# every arm of this checker, strict mode included.

def selfcheck():
    """`--selfcheck`: the strict-mode detector against a table of shapes.

    Every one of these was a real mistake first. The four TRUE rows are shapes
    that reached `main` checked by nothing; the FALSE rows are shapes an earlier
    cut of this detector flagged wrongly -- a valid ref at the end of a sentence,
    and two refs separated by a comma, both of which it swallowed whole.
    """
    # THE FIXTURES ARE ASSEMBLED, NEVER SPELLED. Written literally, this table is
    # a dozen citation-shaped tokens in a file the gate scans -- it flagged six of
    # its own test rows the first time, which is the gate being right and the test
    # being unusable. Same trap caught two explanatory comments in this file
    # before it: write an example citation and you have made a citation.
    A, B = "src/tycho" + "c.c", "tests/ru" + "n.sh"
    tracked = {A, B}
    cases = [
        ("see `%s:100` here" % A,            False, "backticked path:N"),
        ("see `%s:100-200` here" % A,        False, "path:N-M"),
        ("see `%s:100@main` here" % A,       False, "path:N@token"),
        ("see `%s@main` here" % A,           False, "path@SYMBOL"),
        ("see %s:100 here" % A,              False, "bare path:N"),
        ("see http://example.com:8080/x here", False, "a URL with a port"),
        ("see %s:100. Next." % A,            False, "ref ending a sentence"),
        ("see %s:100, %s:5" % (A, B),        False, "two refs, comma-separated"),
        ("see nosuch/thing.c:100,200 here",  False, "path not in the repo"),
        ("see %s:100,200 here" % A,          True,  "bare multi-range"),
        ("see `%s:100,200` here" % A,        True,  "backticked multi-range"),
        ("see `%s:100-200x` here" % A,       True,  "junk after the range"),
        ("see %s:100@foo.bar here" % A,      True,  "dotted anchor, unbackticked"),
    ]
    claimers = (CITE, SYMCITE, SYMCITE_ANY, DOCCITE, SRCCITE, SYMCITE_SRC)
    bad = 0
    for line, want, why in cases:
        got = bool(unclaimed(line, tracked, claimers))
        if got != want:
            bad += 1
            print("  FAIL  %-28s want=%s got=%s  %r" % (why, want, got, line))
    if bad:
        print("citation selfcheck: FAILED (%d of %d)" % (bad, len(cases)))
        return 1
    print("citation selfcheck: ok (%d shapes, %d of them must be flagged)"
          % (len(cases), sum(1 for c in cases if c[1])))
    return 0


def main():
    # Filter in Python rather than passing `*.md` as a pathspec. Handing a
    # wildcard to git THROUGH subprocess loses it on Windows: measured on
    # Windows 11 26200, `git ls-files '*.md'` from the MSYS2 shell answers 126
    # files, and the identical argv through subprocess answers 8 -- the
    # top-level ones. The checker then reported `ok` over 8 anchored and 6 bare
    # citations instead of 134 and 812, which is the dangerous failure: a gate
    # that passes because it checked almost nothing. Same class as the
    # posixpath bug in check_goldens.py (1ca7e80).
    mds = [f for f in subprocess.run(["git", "ls-files"], cwd=ROOT,
                                     capture_output=True, text=True,
                                     check=True).stdout.split()
           if f.endswith(".md")]
    fails, n_bare, n_anchored, n_prov = [], 0, 0, 0
    n_bare_prov = 0
    n_sym, n_sym_src = 0, 0
    drift_single, drift_wide = [], []
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
            # A MALFORMED ANCHOR IS A FAILURE, NOT A SKIP. Same SRC_PREFIX
            # filter as above, which is what keeps an email address, an npm
            # `pkg@version` or an `@decorator` out: none of them is a tracked
            # path immediately followed by `@`.
            for m in SYMCITE_ANY.finditer(line):
                sp, sym = m.group(1), m.group(2)
                if not (sp.startswith(SRC_PREFIX) or sp == "Makefile"):
                    continue
                if SYM_OK.fullmatch(sym):
                    continue
                fails.append(
                    "%s:%d  `%s` -> MALFORMED ANCHOR: '%s' is not one "
                    "symbol-shaped token, so `%s@...` matches nothing this gate "
                    "checks and the ref only LOOKS policed. Anchor a single "
                    "`[A-Za-z0-9_]` token, or give the line and anchor there: "
                    "`%s:<line>@%s`."
                    % (md, ln, m.group(0).strip("`"), sym, sp, sp, sym))
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
                    if b == a:
                        drift_single.append((md, ln, m.group(0).strip("`"),
                                             cur, a, src[a - 1].strip()[:60]))
                    elif b - a + 1 >= REPORT_WIDE:
                        drift_wide.append((b - a + 1, md, ln,
                                           m.group(0).strip("`"), cur))
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
    n_hash = commit_hashes(srcs, fails)
    if n_hash == -1:
        print("commit-hash check: SKIPPED (shallow clone -- no history to "
              "resolve a hash against; nothing here is at fault)")
    if "--report" in sys.argv:
        # ADVISORY ONLY. Neither list is a defect: it is where drift hides.
        print("--- drift-prone refs (advisory; no verdict attached) ---")
        print("%d un-anchored single-line refs. Each proves a line EXISTS, not "
              "that it still says what the citing text claims. `path:N@token` "
              "is the form that reddens on drift:" % len(drift_single))
        for md, ln, raw, cur, a, txt in drift_single:
            print("    %s:%d  `%s`  %s:%d reads: %s"
                  % (md, ln, raw, cur, a, txt or "(blank)"))
        print("%d ranges of %d+ lines. A wide range is often honest (a whole "
              "parser); it is also where a re-point goes unnoticed:"
              % (len(drift_wide), REPORT_WIDE))
        for w, md, ln, raw, cur in sorted(drift_wide, reverse=True):
            print("    %4d lines  %s:%d  `%s`" % (w, md, ln, raw))
        print("--- end advisory ---")
    # --- strict mode: the leftovers no strict parser claimed ----------------
    # Only parsers that actually CHECK a ref belong here. Two shape-DETECTORS
    # sat in this tuple for one revision and that was a bug: claiming a token
    # told strict mode "handled" while the detector's own arm did not run for
    # that file type, so a bare `path:N,M` in Markdown went unreported by both.
    # Strict mode subsumes them, which is the point of inverting the question:
    # one rule that asks "did anything check this?" instead of a list of the
    # bad shapes seen so far.
    CLAIMERS = (CITE, SYMCITE, SYMCITE_ANY, DOCCITE, SRCCITE, SYMCITE_SRC)
    leftovers = []
    for f in srcs:
        if f.endswith(SKIP_SUFFIX) or f in SKIP_CITER:
            continue
        if not (f.endswith(".md") or f.startswith(DOC_SCAN_PREFIX)):
            continue
        try:
            fh = open(os.path.join(ROOT, f), errors="replace")
        except (IsADirectoryError, FileNotFoundError):
            continue
        with fh:
            for ln, line in enumerate(fh, 1):
                for tok in unclaimed(line, tracked, CLAIMERS):
                    leftovers.append("%s:%d  %s" % (f, ln, tok))
    # A LEFTOVER IS A FAILURE. It was an advisory for exactly as long as it took
    # to get the tree to zero (2026-08-14), which is the only honest moment to
    # turn one of these on: no flag day, and the next one that appears is the
    # author's own line rather than an inherited backlog.
    for lo in leftovers:
        hint = (" A multi-range (`path:N-M,X-Y`) is the common case: write one "
                "citation per range." if "," in lo.split()[-1] else "")
        fails.append("%s -> UNPARSEABLE CITATION: citation-shaped, and no rule "
                     "in this gate claims it, so it is checked by NOTHING. "
                     "Rewrite it in a form the gate parses (`path:N`, "
                     "`path:N-M`, `path:N@token`, `path@SYMBOL`) or stop making "
                     "it look like a citation.%s" % (lo, hint))

    if "--stats" in sys.argv:
        print("citation check: %d anchored (content-checked, %d of them the "
              "mandatory `> Provenance:` single-line refs), %d bare (bounds "
              "only, %d exempt `> Provenance:` ranges), %d source->doc "
              "(existence), %d source->source (bounds), %d source->source "
              "anchored (content-checked), %d `path@SYMBOL` definition refs "
              "(%d of them from source), %d distinct commit hashes"
              % (n_anchored, n_prov, n_bare, n_bare_prov, n_doc, n_src,
                 n_src_anch, n_sym + n_sym_src, n_sym_src, max(n_hash, 0)))
    if fails:
        for f in fails:
            print("STALE  " + f)
        print("citation check: FAILED (%d stale citation(s) above)" % len(fails))
        return 1
    print("citation check: ok (%d anchored contain the token they name and each "
          "names one line, %d bare in bounds, %d source->doc citations resolve, "
          "%d source->source in bounds, %d source->source anchored, "
          "%d `path@SYMBOL` definition refs name a symbol still in their file, "
          "%d commit hashes resolve)"
          % (n_anchored, n_bare, n_doc, n_src, n_src_anch, n_sym + n_sym_src,
             max(n_hash, 0)))
    return 0


if __name__ == "__main__":
    if "--selfcheck" in sys.argv:
        sys.exit(selfcheck())
    sys.exit(main())
