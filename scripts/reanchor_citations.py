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
`src/tychoc.c:10608` -- a line whose exact text occurs 5 times in the file --
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
     SOURCE file -- `src/tychoc.c:10550, :9666`. The checker's SRCCITE pattern
     requires a path, so the second ref is unchecked.
  2. the same inside `.ty` fixture comments.
  3. a file's own `(:4592)`-style self-references.

  Find them with:  git grep -nE '[ (]:[0-9]{3,5}\\b' -- <path>

WHICH FILES IT RE-ANCHORS. With no positional argument it takes EVERY file the
diff against <ref> moved that something else in the tree cites, and re-anchors
all of them in one pass; changed files nothing cites are named and skipped. The
old default was the single file `src/tychoc.c`, and a commit that also moved
`runtime/tycho_rt.c` silently left its refs stale -- 20 of them across 13
documents, observed 2026-09-02. Naming files explicitly still overrides.

USAGE
  python3 scripts/reanchor_citations.py                       # dry run vs HEAD
  python3 scripts/reanchor_citations.py --apply               # write
  python3 scripts/reanchor_citations.py runtime/tycho_rt.c --apply
  python3 scripts/reanchor_citations.py --ref HEAD~3 --apply
  python3 scripts/reanchor_citations.py --selfcheck
  python3 scripts/reanchor_citations.py --forget          # drop the state below

RUNNING IT TWICE. It used to double-shift: the map is built from <ref> to the
worktree, so a second `--apply` moved citations the first run had already
moved, onto real lines that read plausibly, with the gate still green. Observed
2026-09-03 -- `rewrote 68 file(s); 1 citation(s) need a human`, then `rewrote 68
file(s); 7` -- and the tree had to be reset.

The fix is state, not a refusal, because a bare `path:N` carries no token to
verify against and "already correct" is therefore undecidable from content
alone. On `--apply` each target's worktree bytes are written as a git blob and
recorded in `$GIT_DIR/reanchor_citations.json`; the next run uses THAT as the
old side instead of `<ref>:<path>`. A second run in a row then maps a file
against itself, the identity map short-circuits, and nothing is written --
idempotence falls out of using the right old side rather than out of a special
case. Editing the target further and re-running is correct for the same reason:
the delta is measured from where the citations actually point.

The state can go stale (a reset, or a rewritten document edited by hand). If any
file the last `--apply` wrote no longer matches what it wrote, the recorded
anchor may be a lie, so `--apply` REFUSES rather than guessing; `--forget`
clears the record and `--ignore-state` reproduces the old ref-relative
behaviour deliberately.
"""
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                      capture_output=True, text=True, check=True).stdout.strip()
GITDIR = subprocess.run(["git", "rev-parse", "--absolute-git-dir"],
                        capture_output=True, text=True, check=True).stdout.strip()
STATE = os.path.join(GITDIR, "reanchor_citations.json")

# `path:N`, `path:N-M`, `path:N-M@token` in Markdown, backticked; and the same
# form unbackticked in a source file. Both mirror check_citations.py.
CITE = re.compile(r'`(?:([A-Za-z0-9_./-]+\.[A-Za-z0-9]+))?:(\d+)(?:-(\d+))?'
                  r'(?:@([^`]+))?`')
SRCCITE = re.compile(r'((?:[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]+)|Makefile)'
                     r':(\d+)(?:-(\d+))?(?:@([A-Za-z0-9_]+))?')
HOSTPORT = re.compile(r'^\d+(?:\.\d+)+$')   # 127.0.0.1 is not a path and a line

SKIP = set()


def git(*args, check=False):
    return subprocess.run(["git"] + list(args), capture_output=True, text=True,
                          check=check, cwd=ROOT)


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def load_state():
    """The recorded anchor, or {} if there is none or it can no longer be trusted."""
    try:
        st = json.load(open(STATE))
    except (OSError, ValueError):
        return {}
    stale = []
    for path, want in sorted(st.get("wrote", {}).items()):
        full = os.path.join(ROOT, path)
        got = digest(open(full).read()) if os.path.isfile(full) else None
        if got != want:
            stale.append(path)
    if stale:
        st["stale"] = stale
    return st


def save_state(targets, texts):
    wrote = {p: digest(t) for p, t in texts.items()}
    anchored = {}
    for t in targets:
        blob = git("hash-object", "-w", "--", os.path.join(ROOT, t), check=True)
        anchored[t] = blob.stdout.strip()
    json.dump({"anchored": anchored, "wrote": wrote}, open(STATE, "w"), indent=1)


def old_side(target, ref, state):
    """(lines, label) of the content the tree's citations currently point into."""
    sha = state.get("anchored", {}).get(target)
    if sha and git("cat-file", "-e", sha + "^{blob}").returncode == 0:
        return git("cat-file", "blob", sha, check=True).stdout.split("\n"), \
            "last --apply"
    r = git("show", "%s:%s" % (ref, target))
    if r.returncode != 0:
        return None, ref   # absent at <ref>: no old line numbers exist to map FROM
    return r.stdout.split("\n"), ref


def line_map(old, new):
    m = {}
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(
            None, old, new, autojunk=False).get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                m[i1 + k + 1] = j1 + k + 1
    return m


def tracked_texts():
    texts = {}
    for path in subprocess.run(["git", "ls-files"], capture_output=True,
                               text=True, check=True).stdout.split("\n"):
        if not path or path in SKIP or path.endswith((".out", ".err")):
            continue
        full = os.path.join(ROOT, path)
        if not os.path.isfile(full):
            continue
        try:
            texts[path] = open(full, errors="strict").read()
        except (UnicodeDecodeError, IsADirectoryError):
            continue
    return texts


def moved_files(ref):
    return [p for p in subprocess.run(
        ["git", "diff", "--name-only", ref, "--"], capture_output=True,
        text=True, check=True).stdout.split("\n")
        if p and os.path.isfile(os.path.join(ROOT, p))]


def reanchor(target, ref, texts, touched, state):
    """Rewrite every citation to `target` in `texts`; return (files, dropped)."""
    old, label = old_side(target, ref, state)
    if old is None:
        # A file NEW in the working tree has no old side. Every citation to it was
        # written against the copy on disk, so there is nothing to shift -- but
        # aborting the whole run on it (exit 128) meant a phase that added a lane
        # and edited src/tychoc.c in one commit could not re-anchor at all.
        print("%s: new since %s; no old line numbers to map from -- skipped"
              % (target, label))
        return 0, 0
    new = open(os.path.join(ROOT, target)).read().split("\n")
    lmap = line_map(old, new)
    print("%s: %d of %d lines survive %s..worktree (%d -> %d lines)"
          % (target, len(lmap), len(old), label, len(old), len(new)))
    if len(old) == len(new) and all(k == v for k, v in lmap.items()):
        print("  no line moved; nothing to re-anchor")
        return 0, 0

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

    for path in sorted(texts):
        text = texts[path]
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
            texts[path] = "\n".join(out)
            touched.add(path)
    return moved, dropped


MARK = "int marker(void) { return 1; }"


def selfcheck():
    """Five cases in a sandbox repo; [2] is the control that must still fail."""
    import tempfile

    def me(repo, *a):
        return subprocess.run([sys.executable, os.path.abspath(__file__)] + list(a),
                              cwd=repo, capture_output=True, text=True)

    def build(td, name):
        repo = os.path.join(td, name)
        os.makedirs(os.path.join(repo, "pkg"))
        os.makedirs(os.path.join(repo, "notes"))
        body = ["int pad%d(void) { return %d; }" % (i, i) for i in range(1, 21)]
        body[9] = MARK
        open(os.path.join(repo, "pkg/f.c"), "w").write("\n".join(body) + "\n")
        open(os.path.join(repo, "notes/d.md"), "w").write(
            "the marker is at `pkg/f.c:10@marker`.\n")
        for a in (["init", "-q"], ["add", "-A"],
                  ["-c", "user.email=s@x", "-c", "user.name=s", "commit", "-qm", "b"]):
            subprocess.run(["git"] + a, cwd=repo, capture_output=True, check=True)
        return repo

    def insert(repo, n):
        f = os.path.join(repo, "pkg/f.c")
        old = open(f).read().split("\n")
        open(f, "w").write("\n".join(["// added"] * n + old))

    def doc(repo):
        return open(os.path.join(repo, "notes/d.md")).read().strip()

    def truth(repo):
        return open(os.path.join(repo, "pkg/f.c")).read().split("\n").index(MARK) + 1

    fails = []

    def want(tag, got, exp):
        print("  [%s] %s%s" % (tag, got, "" if got == exp else "   WANT %s" % (exp,)))
        if got != exp:
            fails.append(tag)

    with tempfile.TemporaryDirectory() as td:
        r = build(td, "idem")
        insert(r, 5)
        me(r, "--apply")
        first = doc(r)
        me(r, "--apply")
        want("1a", first, "the marker is at `pkg/f.c:%d@marker`." % truth(r))
        want("1b", doc(r), first)

        r = build(td, "control")
        insert(r, 5)
        me(r, "--apply", "--ignore-state")
        one = doc(r)
        me(r, "--apply", "--ignore-state")
        want("2", doc(r) != one, True)

        r = build(td, "incremental")
        insert(r, 5)
        me(r, "--apply")
        insert(r, 3)
        me(r, "--apply")
        want("3", doc(r), "the marker is at `pkg/f.c:%d@marker`." % truth(r))

        r = build(td, "stale")
        insert(r, 5)
        me(r, "--apply")
        d = os.path.join(r, "notes/d.md")
        open(d, "a").write("a human edited this line\n")
        before = doc(r)
        rc = me(r, "--apply").returncode
        want("4a", rc, 2)
        want("4b", doc(r), before)

        # [5] a file NEW in the working tree, staged and CITED. It has no old
        # side, and aborting on it (exit 128) used to kill the whole run --
        # including the re-anchoring of every file that did have one.
        r = build(td, "newfile")
        insert(r, 5)
        open(os.path.join(r, "pkg/g.c"), "w").write("// fresh\n" + MARK + "\n")
        open(os.path.join(r, "notes/d.md"), "a").write(
            "and a new one at `pkg/g.c:2@marker`.\n")
        subprocess.run(["git", "add", "-A"], cwd=r, capture_output=True, check=True)
        out = me(r, "--apply")
        want("5a", out.returncode, 0)
        want("5b", doc(r).split("\n")[0],
             "the marker is at `pkg/f.c:%d@marker`." % truth(r))
        want("5c", "pkg/g.c: new since HEAD" in out.stdout, True)

    print("selfcheck: %s" % ("all green" if not fails else "FAILED " + " ".join(fails)))
    return 1 if fails else 0


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    apply_ = "--apply" in sys.argv
    ref = "HEAD"
    if "--ref" in sys.argv:
        ref = sys.argv[sys.argv.index("--ref") + 1]
        args = [a for a in args if a != ref]

    if "--forget" in sys.argv:
        if os.path.exists(STATE):
            os.remove(STATE)
            print("forgot %s" % STATE)
        else:
            print("no recorded anchor to forget")
        return 0

    state = {} if "--ignore-state" in sys.argv else load_state()
    if state.get("stale"):
        print("recorded anchor is STALE -- these were rewritten by the last "
              "--apply and have changed since:")
        for q in state["stale"]:
            print("  %s" % q)
        print("so where the citations point is no longer known. Re-run with "
              "--ignore-state to map from %s anyway, or --forget to drop the "
              "record." % ref)
        if apply_:
            return 2
        state = {}
    elif state.get("anchored"):
        print("anchored to the last --apply (%d file(s)), not to %s"
              % (len(state["anchored"]), ref))

    texts = tracked_texts()
    if args:
        targets, skipped = args, []
    else:
        targets, skipped = [], []
        # A path nothing else cites has no citation to move; naming it is what
        # stops a silent skip reading like a clean pass.
        for p in moved_files(ref):
            if any(p in t for q, t in texts.items() if q != p):
                targets.append(p)
            else:
                skipped.append(p)
        if not targets:
            print("no cited file moved against %s" % ref)
            for p in skipped:
                print("  skipped %s (nothing cites it)" % p)
            return 0
    print("targets: %s" % " ".join(targets))
    for p in skipped:
        print("  skipped %s (nothing cites it)" % p)

    touched, dropped = set(), 0
    for t in targets:
        _, d = reanchor(t, ref, texts, touched, state)
        dropped += d

    for path in sorted(touched):
        print("  %s %s" % ("rewrote" if apply_ else "would rewrite", path))
        if apply_:
            open(os.path.join(ROOT, path), "w").write(texts[path])
    if apply_:
        save_state(targets, {q: texts[q] for q in touched})

    print("%s %d file(s); %d citation(s) need a human"
          % ("rewrote" if apply_ else "would rewrite", len(touched), dropped))
    if not apply_ and touched:
        print("(dry run -- re-run with --apply, then check_citations.py)")
    return 1 if dropped else 0


sys.exit(main())
