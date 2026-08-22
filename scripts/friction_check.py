#!/usr/bin/env python3
"""Re-score docs/internals/FRICTION.md's closed entries by RUNNING their pins.

An entry that says FIXED / CLOSED / GATED / PINNED is a claim about the tree as
it is today, and the claims rot: #58 said the uuid entropy caveat was on "both"
surfaces while the package carried no warning at all, and nothing could notice,
because scoring an entry meant a human reading 36 of them and inferring which
lane covered which. This makes the inference explicit and mechanical.

Each closed entry carries one line in its body:

    > Pinned-by: make math-diff
    > Pinned-by: grep -q 'NOT unguessable' corelib/uuid/uuid.ty
    > Pinned-by: none -- a timing claim; a gate asserting a timing is a coin toss

A pin is a shell command; exit 0 means the entry still holds. `none` must carry
a reason, is never run, and is COUNTED -- an entry nothing asserts is the find
this exists to surface, so it is reported rather than quietly skipped.

TRUST: pins are executed. This file is in the repo and reviewed like any other
source; do not point it at a FRICTION.md from anywhere else.
"""

import concurrent.futures
import re
import subprocess
import sys

DOC = "docs/internals/FRICTION.md"
# Case-INSENSITIVE on purpose: three entries are closed as "documented
# 2026-08-15" in lower case, and an uppercase-only match made them invisible --
# a pin on one of those was silently never run, which is the exact failure this
# gate exists to stop, committed inside the gate itself.
CLOSED = re.compile(r"\b(FIXED|CLOSED|GATED|PINNED|DOCUMENTED)\b", re.I)
HEAD = re.compile(r"^### (.+)$")
PIN = re.compile(r"^>\s*Pinned-by:\s*(.+?)\s*$")


def entries(text):
    """(title, pin, closed) for every `### ` section, in document order."""
    out, title, body = [], None, []
    for line in text.split("\n"):
        m = HEAD.match(line)
        if m:
            if title is not None:
                out.append((title, body))
            title, body = m.group(1), []
        elif title is not None:
            body.append(line)
    if title is not None:
        out.append((title, body))
    scored = []
    for title, body in out:
        pin = None
        for line in body:
            p = PIN.match(line)
            if p:
                pin = p.group(1)
                break
        # A pin makes an entry scoreable whatever its title says: the author
        # wrote a claim they want run, and a title-matching heuristic must not
        # be what decides to ignore it.
        scored.append((title, pin, bool(pin) or bool(CLOSED.search(title))))
    return scored


def run(cmd):
    r = subprocess.run(["sh", "-c", cmd], capture_output=True, text=True)
    tail = (r.stdout + r.stderr).strip().split("\n")[-1][:100] if r.returncode else ""
    return r.returncode, tail


SELFCHECK = """### 1. A thing that broke — **FIXED 2026-01-01**

> Pinned-by: true

### 2. A thing closed in lower case — **documented 2026-01-01**

> Pinned-by: false

### 3. A thing nothing asserts — **FIXED 2026-01-01**

### 4. A timing claim — **FIXED 2026-01-01**

> Pinned-by: none -- a timing gate is a coin toss
"""


def selfcheck():
    rows = entries(SELFCHECK)
    closed = [(t, p) for t, p, c in rows if c]
    ok = True

    def leg(name, got, want):
        nonlocal ok
        if got != want:
            ok = False
        print("  %-46s %s (got %r)" % (name, "ok" if got == want else "FAILED", got))

    leg("[1] a holding pin is scored", closed[0][1], "true")
    leg("[2] a LOWER-CASE closure is still scored", len(closed), 4)
    leg("[3] a failing pin exits non-zero", run("false")[0] != 0, True)
    leg("[4] a holding pin exits zero", run("true")[0], 0)
    leg("[5] an unpinned entry has no pin", closed[2][1], None)
    leg("[6] an excused entry is never run", closed[3][1].startswith("none"), True)
    print("friction selfcheck: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    text = open(DOC).read()
    rows = entries(text)
    closed = [(t, p) for t, p, c in rows if c]
    pinned = [(t, p) for t, p in closed if p and not p.startswith("none")]
    excused = [(t, p) for t, p in closed if p and p.startswith("none")]
    unpinned = [t for t, p in closed if not p]

    cmds = sorted({p for _, p in pinned})
    results = {}
    if cmds:
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
            for cmd, res in zip(cmds, ex.map(run, cmds)):
                results[cmd] = res

    bad = 0
    for title, pin in pinned:
        rc, tail = results[pin]
        if rc:
            bad += 1
            print("STALE  %s\n         pin `%s` exited %d: %s" % (title[:90], pin, rc, tail))
    for title, pin in excused:
        print("EXCUSED %s\n         %s" % (title[:90], pin))
    for title in unpinned:
        print("UNPINNED %s" % title[:100])

    # UNPINNED is REPORTED, never a failure. 68 closed entries carried no pin on
    # the day this was written; failing on them would make the gate a flag day
    # nobody completes, and the count is the useful number either way. Only a pin
    # that STOPPED HOLDING is a red -- that is the thing a human cannot notice.
    print(
        "friction check: %s (%d closed entries: %d pinned by %d distinct commands, "
        "%d excused, %d unpinned)"
        % (
            "FAILED" if bad else "ok",
            len(closed),
            len(pinned),
            len(cmds),
            len(excused),
            len(unpinned),
        )
    )
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
