#!/usr/bin/env python3
"""Runtime-surface lane -- one compiler, a written-down oracle (since 2026-07-30).

WHAT THIS ASSERTS NOW
---------------------
Compile tests/rtparity/surface.ty with tychoc, read the emitted C, and check the
three USER-OBSERVABLE runtime surfaces against the EXPECT sets below:

  1. env knobs   -- the literal getenv("...") names a compiled program reads.
  2. diagnostics -- the "tycho: ..." abort/trap texts a compiled program can
                    print. A missing text == a missing check.
  3. stats rows  -- the row labels of the TYCHO_ARENA_STATS summary. The knob can
                    be present while the report it drives is half-empty.

Both directions fail: an expected item that stopped being emitted, and an
emitted item nobody wrote down. The second half is what keeps the list honest --
a new trap must be added here with a reason, it cannot drift in unremarked.

WHY AN ORACLE, AND WHY NOT THE OTHER TWO OPTIONS
------------------------------------------------
This lane checks the emitted runtime SURFACE against an oracle: every env knob,
every "tycho: ..." trap text and every arena-stats row the runtime defines must
reach the emitted C, and nothing may reach it unrecorded.

  * RETIREMENT was rejected. Unlike the two-runtime *spelling* comparison, the
    drift class this lane exists to catch is single-implementation: "the runtime
    DEFINES a knob / trap / stats row, and the emitted program does not actually
    WIRE IT UP". That is exactly the TYCHO_ARENA_STATS bug (present in
    runtime/tycho_rt.c, a silent no-op in every emitted binary) and this lane
    detects it from one implementation.
  * A PURE PROPERTY CHECK was rejected as the whole answer, because at this
    arity the strongest available property is nearly vacuous: tychoc embeds
    runtime/tycho_rt.c verbatim (asserted below as `rt_subset`), so every
    runtime-defined surface item reaches the emitted C by construction, and
    deleting a getenv from the runtime moves BOTH sides together. The property
    is kept -- it is the guard for a future tychoc that emits only the parts of
    the runtime a program uses -- but on its own it would catch nothing today.
  * THE EXPECT ORACLE was chosen, in the shape phase 22 gave
    fuzz/run_typeparity.py. Note what "table" has to mean here: 36 items, each
    one a user-facing contract, each hand-checkable -- unlike typeparity's 4608
    rows, an enumerated list at this size is a golden, not a photograph, and it
    is the same mechanism tests/diag/*.err uses for diagnostic text.

WHAT IT STILL DOES NOT BUY
--------------------------
The EXPECT sets were recorded off the compiler they gate, so a trap that has
ALWAYS been wrong, or a knob that has ALWAYS been read into nothing, is
invisible here -- this lane sees a surface item vanish, not a surface item that
never worked. Testing that a knob has EFFECT is tests/ and tests/conc/'s job.
See ROADMAP.md and docs/architecture.md.

Usage:  python3 tests/rtparity/run.py        (or `make rtparity`)
Exit 0 = the emitted runtime surface matches the oracle; nonzero = drift, naming
the symbol.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROBE = os.path.join(ROOT, "tests", "rtparity", "surface.ty")
RUNTIME = os.path.join(ROOT, "runtime", "tycho_rt.c")

# --- The oracle ---------------------------------------------------------------
# Every entry is a surface a compiled Tycho program exposes to its user. Recorded
# by hand off the emitted C and read one by one; an entry disappearing is a
# capability disappearing, an entry appearing unlisted is a capability nobody
# wrote down.

# runtime/tycho_rt.c:428, :567, :848 -- the only literal getenv() names in the
# runtime. The wrapper Tycho code calls takes a runtime string (getenv(name)), so
# a user program's own env reads cannot land here.
EXPECT_ENV = {
    "TYCHO_ARENA_STATS",
    "TYCHO_BLOCK",      # block-size override for arena sweeps (2026-08-04, arena-observability phase)
    "TYCHO_MAX_TASKS",
    "TYCHO_THREADS",
}

# The TYCHO_ARENA_STATS report's row labels (runtime/tycho_rt.c:369-396).
EXPECT_ROWS = {
    "arenas",
    "block reuse",
    "bump-alloc",
    "peak live",
    "recycle",      # free-list hit count + bytes (2026-08-04, arena-observability phase)
    "OS reserved",
    "time",         # wall, OS-malloc and teardown nanoseconds (2026-08-17). The bump
                    # path is deliberately NOT timed -- see the note at st_ns_os in
                    # runtime/tycho_rt.c -- so this row reports three figures, not four.
}

# Traps that live in the runtime itself. Truncated entries (`tycho: chr(%`) are
# not typos: those diagnostics are a run of adjacent C literals with a PRId64
# macro between them, so the extractor sees the text up to the first splice.
EXPECT_MSG_RUNTIME = {
    r"tycho: [int:float] map exceeds 2^31 entries\n",
    r"tycho: [int:int] map exceeds 2^31 entries\n",
    r"tycho: [string:float] map exceeds 2^31 entries\n",
    r"tycho: [string:int] map exceeds 2^31 entries\n",
    # Both from `51dcb45b` (packed): from_bytes() validates the layout at RUN
    # time rather than reinterpreting the bytes. runtime/tycho_rt.c:1905, :1909.
    r"tycho: a packed field width is not 1, 2, 4 or 8",
    r"tycho: a packed struct's field widths do not sum to its size",
    r"tycho: channel already closed\n",
    r"tycho: channel capacity must be >= 1\n",
    r"tycho: chr(%",
    r"tycho: division by zero\n",
    r"tycho: division overflow\n",
    r"tycho: element-wise arithmetic on arrays of different lengths ",
    r"tycho: float-to-int conversion out of range\n",
    r"tycho: index %",
    r"tycho: modulo by zero\n",
    r"tycho: negative shift count\n",
    r"tycho: out of memory\n",
    r"tycho: pop from an empty array\n",
    r"tycho: reserve capacity %",
    r"tycho: send on a closed channel\n",
    r"tycho: spawn failed (cannot create thread)\n",
    r"tycho: split with an empty separator\n",
    r"tycho: stack overflow -- recursion too deep\n",  # the `docs/internals/plan-tycho-scheme-DONE.md` phase-1 guard: deep generated-code recursion fails closed instead of SIGSEGV
    r"tycho: string index %",
    r"tycho: string length %",
    r"tycho: task already waited\n",
    r"tycho: too many concurrent tasks (max %",
}

# Traps that are NOT in the runtime file: src/tychoc.c writes them inline, ON
# DEMAND, so each one is present only because surface.ty contains the construct
# that triggers it. These are the entries the lane earns its keep on -- a codegen
# arm that stops emitting its guard reddens here and nowhere else.
EXPECT_MSG_CODEGEN = {
    r"tycho: non-exhaustive match\n",     # src/tychoc.c:11673, :10782
    r"tycho: push to a full bounded[4]\n",  # src/tychoc.c:13501 (the [4] is surface.ty's Inline.slots)
    r"tycho: slice [%",                   # src/tychoc.c:11299, :9711
}
# REMOVED 2026-07-30 (the loops-cleanup plan): r"tycho: range step is zero\n".
# The oracle was out of date, not the codegen. `range(a, b, step)` went on
# 2026-07-29 and left the step machinery unreachable; phase 53 deleted the `Stmt`
# field, the step codegen, this abort and the direction ternary. No construct can
# reach the trap, so nothing emits it. This lane FOUND that -- it was the only
# gate in the tree that noticed the trap text disappear, which is the argument for
# wiring it into `make ci` (step [2d/13], same phase).
EXPECT_MSG = EXPECT_MSG_RUNTIME | EXPECT_MSG_CODEGEN

# --- Extraction --------------------------------------------------------------
RE_ENV = re.compile(r'getenv\("([A-Za-z_][A-Za-z0-9_]*)"\)')
RE_MSG = re.compile(r'"(tycho: [^"]*)"')
# Arena-stats rows: `  <label>:` at the start of a report line, after an opening
# quote or an embedded \n.
RE_ROW = re.compile(r'(?:\\n|")  ([A-Za-z][A-Za-z0-9 -]{2,}):')

# Anti-vacuity floors. If extraction ever stops working (an --emit-c format
# change, a moved runtime) every set goes empty, and the missing/extra diff would
# then report thirty-six separate "missing" lines rather than the one true fact:
# the extractor is broken. The floor says that instead.
FLOOR = {"env knob": 3, "diagnostic": 25, "arena-stats row": 5}

SURFACES = (
    ("env knob", RE_ENV, EXPECT_ENV),
    ("diagnostic", RE_MSG, EXPECT_MSG),
    ("arena-stats row", RE_ROW, EXPECT_ROWS),
)


def err(msg):
    """Report to stderr, ordered against the stdout 'ok' lines -- two
    independently buffered streams otherwise print the verdict before the
    findings it summarises."""
    sys.stdout.flush()
    print(msg, file=sys.stderr, flush=True)


def die(msg):
    err("rtparity: " + msg)
    sys.exit(2)


def emitted_c(tmp):
    """Compile the probe with tychoc; return the emitted C."""
    # On Windows the built compiler is tychoc.exe. Probe for the suffix rather
    # than branching on os.name: MSYS2's python reports os.name == "posix" even
    # though `make` produced a .exe, so the name test would miss it there. Same
    # class of Windows-only harness bug as the posixpath one in check_goldens.py.
    tychoc = os.environ.get("TYCHOC")
    if not tychoc:
        tychoc = os.path.join(ROOT, "tychoc")
        if not os.access(tychoc, os.X_OK) and os.access(tychoc + ".exe", os.X_OK):
            tychoc += ".exe"
    if not os.access(tychoc, os.X_OK):
        die("no %s -- run 'make' first" % tychoc)

    out_c = os.path.join(tmp, "surface_tychoc")
    r = subprocess.run([tychoc, PROBE, "--emit-c", "-o", out_c],
                       capture_output=True, text=True)
    if r.returncode != 0:
        die("tychoc could not compile the probe:\n" + r.stdout + r.stderr)
    with open(out_c + ".c", encoding="utf-8", errors="replace") as f:
        return f.read()


def check(kind, pat, expect, c_src):
    """Compare one surface of the emitted C against the oracle. Returns fails."""
    got = set(pat.findall(c_src))

    if len(got) < FLOOR[kind]:
        err("rtparity: FAIL - only %d %s(s) extracted from the emitted C "
            "(expected >= %d).\n"
            "          That is broken extraction, not drift -- read the regex "
            "before the oracle." % (len(got), kind, FLOOR[kind]))
        return 1

    fails = 0
    for sym in sorted(expect - got):
        err('rtparity: FAIL - %s "%s" is in the oracle but NOT emitted. A runtime '
            'capability\n          disappeared, or the construct in '
            'tests/rtparity/surface.ty that pulls it in did.' % (kind, sym))
        fails += 1
    for sym in sorted(got - expect):
        err('rtparity: FAIL - %s "%s" is emitted but NOT in the oracle. Add it to '
            'tests/rtparity/run.py\n          with a reason -- new user-visible '
            'surface is a deliberate act, not a diff.' % (kind, sym))
        fails += 1

    if fails == 0:
        print("rtparity: %-16s %2d/%2d as recorded (ok)"
              % (kind + "s", len(got), len(expect)), flush=True)
    return fails


def rt_subset(c_src):
    """The property leg, needing no recorded list: everything the runtime file
    DEFINES must reach the emitted program. Trivially true while tychoc embeds
    runtime/tycho_rt.c verbatim -- and the only thing that would notice a future
    tychoc that emits the runtime piecewise."""
    with open(RUNTIME, encoding="utf-8", errors="replace") as f:
        rt = f.read()
    fails = 0
    for kind, pat, _ in SURFACES:
        missing = sorted(set(pat.findall(rt)) - set(pat.findall(c_src)))
        for sym in missing:
            err('rtparity: FAIL - %s "%s" is defined in runtime/tycho_rt.c but does '
                'not reach\n          the emitted C. The runtime is no longer '
                'embedded whole.' % (kind, sym))
            fails += 1
    if fails == 0:
        print("rtparity: runtime file  every defined surface reaches the emitted C (ok)",
              flush=True)
    return fails


def main():
    tmp = tempfile.mkdtemp(prefix="rtparity.")
    try:
        c_src = emitted_c(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    fails = sum(check(kind, pat, expect, c_src) for kind, pat, expect in SURFACES)
    fails += rt_subset(c_src)

    if fails:
        err("\nrtparity: FAIL - %d runtime-surface difference(s) against the oracle "
            "in\n          tests/rtparity/run.py. Either the emitted runtime lost "
            "something, or\n          the oracle is out of date -- decide which, and "
            "say so where you fix it." % fails)
        return 1
    print("rtparity: emitted runtime surface matches the oracle "
          "(%d env knobs, %d diagnostics, %d stats rows)"
          % (len(EXPECT_ENV), len(EXPECT_MSG), len(EXPECT_ROWS)), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
