#!/usr/bin/env python3
"""Every document under `docs/` must be REACHABLE from an index, not merely linked-to.

WHY THIS EXISTS. `scripts/check_links.sh` fails a link that points nowhere and
says nothing about a document nobody points at. `docs/bootstrap.md` sat orphaned
for days and was found by a human reading the index, not by a gate
(`docs/internals/FRICTION.md`, open-list item 13). A dead link is loud; an
invisible document is silent, and silence is the failure mode worth gating.

WHAT "REACHABLE" MEANS HERE, and why not something broader:

  ROOTS are `README.md` and `docs/README.md`. Reachability is transitive over
  Markdown links from there.

  A FILE LINK reaches that file. A DIRECTORY LINK reaches only that directory's
  index (`README.md` / `index.md`), NOT its contents -- `docs/README.md` links
  `guides/`, `internals/` and `rfc/` as bare directories, and counting a
  directory listing as reachability would make this gate VACUOUS for exactly the
  case that motivated it: a new file dropped into a listed directory would be
  "reachable" the moment it existed. Measured 2026-08-13: under the permissive
  rule 0 of 76 docs are unreachable, under this one 13 are.

  SUBJECT is `docs/**.md` only. The other 43 unlinked Markdown files in the tree
  are a different thing and are deliberately out of scope: a `<dir>/README.md` is
  the entry point OF its directory (GitHub renders it on the directory page),
  `bench/*/RESULTS.md` are data beside a benchmark, `.github/*` is consumed by
  GitHub, `plan.md` is a working file. Gating those would be a 43-file flag day
  that says nothing about whether the documentation is navigable.

EXEMPTIONS are listed in EXEMPT below, each with a reason, and are CHECKED: an
exemption naming a file that is now reachable, or that no longer exists, is
itself a failure -- otherwise the list rots into a silencer.
"""
import pathlib
import re
import subprocess
import sys

ROOTS = ["README.md", "docs/README.md"]

# path -> why it is deliberately not reachable. Keep this short; the honest fix
# for an orphan is a link, not an entry here.
EXEMPT = {
    # The nine plan archives. `docs/README.md` and CLAUDE.md both say these were
    # pruned on 2026-08-03 and live on only under the `docs-archive` tag -- they
    # are still tracked, so one of the two is wrong. Exempt pending that
    # decision (delete them, or correct the sentence and index them).
    "docs/internals/plan-repo-polish-DONE.md": "plan archive, pending the prune decision",
    "docs/internals/plan-tycho-chess-DONE.md": "plan archive, pending the prune decision",
    "docs/internals/plan-tycho-kv-DONE.md": "plan archive, pending the prune decision",
    "docs/internals/plan-tycho-kvsrv-DONE.md": "plan archive, pending the prune decision",
    "docs/internals/plan-tycho-rsa-DONE.md": "plan archive, pending the prune decision",
    "docs/internals/plan-tycho-sat-DONE.md": "plan archive, pending the prune decision",
    "docs/internals/plan-tycho-scheme-DONE.md": "plan archive, pending the prune decision",
    "docs/internals/plan-tycho-scheme-compiler-DONE.md": "plan archive, pending the prune decision",
    "docs/internals/plan-tycho-vm-DONE.md": "plan archive, pending the prune decision",
}

LINK = re.compile(r"\]\(([^)]+)\)")
INDEXES = ("README.md", "index.md")


def tracked_md(root: pathlib.Path) -> list:
    out = subprocess.run(["git", "ls-files", "*.md"], cwd=root,
                         capture_output=True, text=True, check=True).stdout.split()
    return [m for m in out if "/content/" not in m]


def targets(root: pathlib.Path, f: str, known: set) -> list:
    """Link targets of `f`, resolved repo-relative. Fences and inline code are
    skipped, the same two exclusions check_links.sh makes."""
    out, fence = [], False
    for line in (root / f).read_text(errors="replace").splitlines():
        if line.lstrip().startswith("```"):
            fence = not fence
            continue
        if fence:
            continue
        for t in LINK.findall(re.sub(r"`[^`]*`", "", line)):
            if t.startswith(("http://", "https://", "#", "mailto:")):
                continue
            p = t.split("#")[0]
            if not p:
                continue
            r = (root / f).parent / p
            if not r.exists():
                continue          # check_links.sh owns the dead-link verdict
            rel = str(r.resolve().relative_to(root))
            if r.is_dir():
                out += [f"{rel}/{i}" for i in INDEXES if f"{rel}/{i}" in known]
            elif rel in known:
                out.append(rel)
    return out


def main() -> int:
    root = pathlib.Path(__file__).resolve().parent.parent
    md = tracked_md(root)
    known = set(md)

    seen, stack = set(), [r for r in ROOTS if r in known]
    while stack:
        f = stack.pop()
        if f in seen:
            continue
        seen.add(f)
        stack += targets(root, f, known)

    docs = [f for f in md if f.startswith("docs/")]
    orphans = [f for f in docs if f not in seen and f not in EXEMPT]
    stale = [f for f in EXEMPT if f not in known or f in seen]

    for f in orphans:
        print(f"ORPHAN  {f}  -- no Markdown links to it; link it from an index "
              f"or add it to EXEMPT in {pathlib.Path(__file__).name} with a reason")
    for f in stale:
        why = "no longer tracked" if f not in known else "is reachable now"
        print(f"STALE-EXEMPT  {f}  -- {why}; drop it from EXEMPT")

    if orphans or stale:
        print(f"reachability: FAILED ({len(orphans)} orphan(s), {len(stale)} stale exemption(s))")
        return 1
    print(f"reachability: ok ({len(docs)} docs reachable from {' + '.join(ROOTS)}; "
          f"{len(EXEMPT)} exempt)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
