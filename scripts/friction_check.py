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
        # ALL of them, not the first: an entry is pinned by a cheap discriminating
        # assertion AND the lane that runs the behaviour, and folding them into
        # one string would defeat the dedup -- 26 entries each spelling
        # "... && make test" ran make test 26 times, 8-way parallel, and blew
        # past ten minutes. Separate lines share one run.
        pin = [m.group(1) for m in (PIN.match(l) for l in body) if m]
        # A pin makes an entry scoreable whatever its title says: the author
        # wrote a claim they want run, and a title-matching heuristic must not
        # be what decides to ignore it.
        scored.append((title, pin, bool(pin) or bool(CLOSED.search(title))))
    return scored


def split(cmds):
    """(heavy, light). A lane already saturates the box (tests/run.sh is
    xargs -P nproc), so running eight of them at once is oversubscription, not
    parallelism: 46 commands 8-way took 9m29 against 2m07 for the same lanes
    2-way. The cheap `test -f`/`grep` pins are IO and stay 8-way.

    The same rule decides what `--light` scores, deliberately: one definition,
    so the ci lane and the standalone run cannot drift into disagreeing about
    which pins are the expensive ones."""
    heavy = [c for c in cmds if c.startswith("make ") or c.startswith("sh scripts/")]
    return heavy, [c for c in cmds if c not in heavy]


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

    leg("[1] a holding pin is scored", closed[0][1], ["true"])
    leg("[2] a LOWER-CASE closure is still scored", len(closed), 4)
    leg("[3] a failing pin exits non-zero", run("false")[0] != 0, True)
    leg("[4] a holding pin exits zero", run("true")[0], 0)
    leg("[5] an unpinned entry has no pin", closed[2][1], [])
    leg("[6] an excused entry is never run", closed[3][1][0].startswith("none"), True)

    # The --light SPLIT, which `make ci` depends on (FRICTION 104). Two ways it
    # could rot silently and both are worse than it not existing: classify
    # everything as light, and the ci lane re-runs the suites it was created to
    # avoid; classify everything as heavy, and the lane scores nothing while
    # still printing ok. These legs pin the rule in both directions.
    sample = ["make test", "sh scripts/math_diff.sh",
              "grep -q 'x' corelib/uuid/uuid.ty", "test -f tests/float_floor.ty",
              'python3 -c "import json"']
    heavy, light = split(sample)
    leg("[7] a suite pin is HEAVY", heavy, ["make test", "sh scripts/math_diff.sh"])
    leg("[8] a fact pin is LIGHT", light, ["grep -q 'x' corelib/uuid/uuid.ty",
                                          "test -f tests/float_floor.ty",
                                          'python3 -c "import json"'])
    leg("[9] the two partition the input", sorted(heavy + light), sorted(sample))
    # An entry pinned ONLY by suites is scored by nothing under --light and must
    # be counted as deferred, never dropped: a subset that does not say what it
    # omitted is indistinguishable from the full run.
    lightset = set(light)
    only_heavy = [("entry pinned only by suites", ["make test"]),
                  ("entry with one fact pin", ["make test", "test -f tests/float_floor.ty"])]
    deferred = [t for t, ps in only_heavy if not any(c in lightset for c in ps)]
    leg("[10] a suite-only entry is DEFERRED, not dropped", deferred,
        ["entry pinned only by suites"])

    print("friction selfcheck: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    lightonly = "--light" in sys.argv
    text = open(DOC).read()
    rows = entries(text)
    closed = [(t, p) for t, p, c in rows if c]
    pinned = [(t, [x for x in p if not x.startswith("none")]) for t, p in closed]
    pinned = [(t, p) for t, p in pinned if p]
    excused = [(t, p) for t, p in closed if p and all(x.startswith("none") for x in p)]
    unpinned = [t for t, p in closed if not p]

    cmds = sorted({c for _, ps in pinned for c in ps})
    heavy, light = split(cmds)
    defer_cmds, defer_entries = 0, []
    if lightonly:
        # --light: score only the pins that assert a SPECIFIC FACT -- the greps,
        # the test -f's, the inline python3 -c's. The `make`/`sh scripts/` pins
        # re-run suites `make ci` has just run, which is what kept this gate out
        # of ci at all (FRICTION 104): +39% wall for coverage ci largely has.
        #
        # An entry whose pins are ALL heavy is scored by nothing in this mode. It
        # must be COUNTED and named in the summary, not silently dropped: a
        # subset that does not say what it left out reads exactly like the full
        # run, which is the failure this whole file exists to prevent.
        lightset = set(light)
        defer_cmds = len(heavy)
        defer_entries = [t for t, ps in pinned if not any(c in lightset for c in ps)]
        cmds, heavy = light, []
        pinned = [(t, [c for c in ps if c in lightset]) for t, ps in pinned]
        pinned = [(t, ps) for t, ps in pinned if ps]
    results = {}
    for group, width in ((light, 8), (heavy, 2)):
        if not group:
            continue
        with concurrent.futures.ThreadPoolExecutor(max_workers=width) as ex:
            for cmd, res in zip(group, ex.map(run, group)):
                results[cmd] = res

    bad = 0
    for title, ps in pinned:
        for pin in ps:
            rc, tail = results[pin]
            if rc:
                bad += 1
                print("STALE  %s\n         pin `%s` exited %d: %s" % (title[:90], pin, rc, tail))
    if not lightonly:
        for title, ps in excused:
            print("EXCUSED %s\n         %s" % (title[:90], ps[0]))
        for title in unpinned:
            print("UNPINNED %s" % title[:100])

    # UNPINNED is REPORTED, never a failure. 68 closed entries carried no pin on
    # the day this was written; failing on them would make the gate a flag day
    # nobody completes, and the count is the useful number either way. Only a pin
    # that STOPPED HOLDING is a red -- that is the thing a human cannot notice.
    if lightonly:
        print(
            "friction check (--light): %s (%d of %d closed entries scored here by "
            "%d fact pins; %d entr%s and %d suite pin(s) DEFERRED to `make "
            "friction-check`, which is the full run)"
            % (
                "FAILED" if bad else "ok",
                len(pinned),
                len(closed),
                len(cmds),
                len(defer_entries),
                "y" if len(defer_entries) == 1 else "ies",
                defer_cmds,
            )
        )
        return 1 if bad else 0
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
