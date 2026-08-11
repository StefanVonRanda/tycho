#!/usr/bin/env python3
"""Re-anchor `path:line` citations after a source file's lines moved.

WHY THIS EXISTS. `scripts/check_citations.py` gates every `path:N` in the tree,
and any insertion into a heavily-cited file invalidates a wall of them at once:
measured over four changes to `src/tychoc.c` on 2026-08-09/10, the counts were
84, 80, 107 and 0. Hand-editing that is error-prone busywork; the mapping is
mechanical, so do it mechanically.

(The 0 is worth knowing about. A refactor that only REWRITES existing lines can
be made line-count-neutral on purpose -- same number of lines in as out -- and
then moves no citation at all. The T_UNBOUND re-sentinel was written that way.
A change that adds real code cannot be, and pays the re-anchor.)

WHAT IT DOES. Builds an old->new line map with difflib over
`git show <ref>:<path>` against the working copy, keeping only the `equal`
blocks, then rewrites every citation that resolves to <path> across
`git ls-files`. Markdown uses the SAME inheritance rule as the checker: a bare
`:N` binds to the last path named in the paragraph, and a blank line resets it.

WHAT IT WILL NOT DO. A citation whose line landed inside a CHANGED hunk has no
correct answer -- the text it named is gone. Those are reported and left alone
for a human. Dry-run is the default; `--apply` writes.

WHEN IT IS SAFE TO RUN. Immediately after YOU moved lines in <path>, while the
rest of the tree is still anchored to <ref>. It rewrites EVERY citation to that
path, so pointing it at a tree where only some refs are stale will shift the
correct ones too and break them. Seen 2026-08-10: a pulled commit moved one line
of `scripts/tools_check.sh`, two refs went stale, and a dry run offered to
rewrite two further files whose refs were already right. Two lines by hand was
the correct fix. **Read the dry run before `--apply`; it is not a formality.**

LIMITATION, measured rather than assumed. The "changed hunk" guard above is NOT
a reliable safety net, because difflib aligns on text, not identity. Deleting
`src/tychoc.c:9860` -- a line whose exact text occurs 5 times in the file --
produced no DROPPED at all: the matcher realigned against one of the duplicates
and the citation was silently remapped to a different line that happens to read
the same. The guard catches a citation into genuinely unique deleted text and
nothing else.

What protects against this is the ANCHORED form: `path:N@token` is verified by
check_citations.py against the new line, so a wrong remap reddens the gate. A
bare `path:N` has no such protection. That is a reason to anchor, and a reason
to run check_citations.py after every `--apply` rather than trusting this
script's own report.

WHAT IT DOES NOT REACH, so check these by hand afterwards (none is policed by
check_citations.py either, which is why they drift silently):

  1. a bare `:N` continuation after a real citation on the same line of a
     SOURCE file -- `src/tychoc.c:9802, :9666`. The checker's SRCCITE pattern
     requires a path, so the second ref is unchecked.
  2. the same inside `.ty` fixture comments.
  3. a file's own `(:4592)`-style self-references.

  Find them with:  git grep -nE '[ (]:[0-9]{3,5}\\b' -- <path>

USAGE
  python3 scripts/reanchor_citations.py                       # dry run vs HEAD
  python3 scripts/reanchor_citations.py --apply               # write
  python3 scripts/reanchor_citations.py runtime/tycho_rt.c --apply
  python3 scripts/reanchor_citations.py --ref HEAD~3 --apply
"""
import difflib
import os
import re
import subprocess
import sys

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                      capture_output=True, text=True, check=True).stdout.strip()

# `path:N`, `path:N-M`, `path:N-M@token` in Markdown, backticked; and the same
# form unbackticked in a source file. Both mirror check_citations.py.
CITE = re.compile(r'`(?:([A-Za-z0-9_./-]+\.[A-Za-z0-9]+))?:(\d+)(?:-(\d+))?'
                  r'(?:@([^`]+))?`')
SRCCITE = re.compile(r'((?:[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]+)|Makefile)'
                     r':(\d+)(?:-(\d+))?(?:@([A-Za-z0-9_]+))?')
HOSTPORT = re.compile(r'^\d+(?:\.\d+)+$')   # 127.0.0.1 is not a path and a line

SKIP = {"compiler/tychoc0.ty"}   # frozen and unfixable; exempt from the gate too


def line_map(old, new):
    m = {}
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(
            None, old, new, autojunk=False).get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                m[i1 + k + 1] = j1 + k + 1
    return m


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    apply_ = "--apply" in sys.argv
    ref = "HEAD"
    if "--ref" in sys.argv:
        ref = sys.argv[sys.argv.index("--ref") + 1]
        args = [a for a in args if a != ref]
    target = args[0] if args else "src/tychoc.c"

    old = subprocess.run(["git", "show", "%s:%s" % (ref, target)],
                         capture_output=True, text=True, check=True
                         ).stdout.split("\n")
    new = open(os.path.join(ROOT, target)).read().split("\n")
    lmap = line_map(old, new)
    print("%s: %d of %d lines survive %s..worktree (%d -> %d lines)"
          % (target, len(lmap), len(old), ref, len(old), len(new)))
    if len(old) == len(new) and all(k == v for k, v in lmap.items()):
        print("no line moved; nothing to re-anchor")
        return 0

    moved = dropped = 0

    def remap(a, b, where):
        nonlocal dropped
        na, nb = lmap.get(a), lmap.get(b)
        if na is None or nb is None:
            print("  DROPPED %s -> :%s%s is inside changed text; fix by hand"
                  % (where, a, "-%d" % b if b != a else ""))
            dropped += 1
            return None
        return na, nb

    for path in subprocess.run(["git", "ls-files"], capture_output=True,
                               text=True, check=True).stdout.split("\n"):
        if not path or path in SKIP or path.endswith((".out", ".err")):
            continue
        full = os.path.join(ROOT, path)
        if not os.path.isfile(full):
            continue
        try:
            text = open(full, errors="strict").read()
        except (UnicodeDecodeError, IsADirectoryError):
            continue
        if target not in text and not path.endswith(".md"):
            continue

        out, cur, changed = [], None, False
        for ln, line in enumerate(text.split("\n"), 1):
            if path.endswith(".md"):
                if not line.strip():
                    cur = None

                def md(m, _ln=ln):
                    nonlocal cur, changed
                    if m.group(1):
                        if HOSTPORT.match(m.group(1)):
                            return m.group(0)
                        cur = m.group(1)
                    if cur != target:
                        return m.group(0)
                    r = remap(int(m.group(2)),
                              int(m.group(3)) if m.group(3) else int(m.group(2)),
                              "%s:%d" % (path, _ln))
                    if r is None:
                        return m.group(0)
                    changed = True
                    return "`%s:%d%s%s`" % (m.group(1) or "", r[0],
                                            "-%d" % r[1] if m.group(3) else "",
                                            "@" + m.group(4) if m.group(4) else "")

                line = CITE.sub(md, line)
            else:
                def src(m, _ln=ln):
                    nonlocal changed
                    if m.group(1) != target:
                        return m.group(0)
                    r = remap(int(m.group(2)),
                              int(m.group(3)) if m.group(3) else int(m.group(2)),
                              "%s:%d" % (path, _ln))
                    if r is None:
                        return m.group(0)
                    changed = True
                    return "%s:%d%s%s" % (m.group(1), r[0],
                                          "-%d" % r[1] if m.group(3) else "",
                                          "@" + m.group(4) if m.group(4) else "")

                line = SRCCITE.sub(src, line)
            out.append(line)

        if changed:
            moved += 1
            print("  %s %s" % ("rewrote" if apply_ else "would rewrite", path))
            if apply_:
                open(full, "w").write("\n".join(out))

    print("%s %d file(s); %d citation(s) need a human"
          % ("rewrote" if apply_ else "would rewrite", moved, dropped))
    if not apply_ and moved:
        print("(dry run -- re-run with --apply, then check_citations.py)")
    return 1 if dropped else 0


sys.exit(main())
