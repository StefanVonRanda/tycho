#!/usr/bin/env python3
"""Runtime-parity lane: the two runtimes must offer the same feature surface.

Tycho ships TWO runtimes. tychoc embeds runtime/tycho_rt.c verbatim into every
file it emits (Makefile:23-26, src/tychoc.c:26). tychoc0 -- the self-hosted
compiler -- writes its OWN runtime out as C string literals (compiler/tychoc0.ty
:9595, `fn preamble`). They are maintained by hand, independently, and until this
lane existed NOTHING compared them.

That is not hypothetical. TYCHO_ARENA_STATS existed in runtime/tycho_rt.c and
was silently a no-op in every binary tychoc0 built, for as long as it took
someone to notice by hand (fixed in 2b24ca6). `make fixpoint` cannot see this
class of bug: it compares tychoc0 against ITSELF byte-for-byte and against
tychoc only BEHAVIOURALLY, on programs that never trip a trap or read an env
knob (compiler/fixpoint.sh:16-30). A runtime feature that is merely *absent*
changes no output on the happy path, so every existing lane stays green.

WHAT IS COMPARED, AND WHY THAT KEY
----------------------------------
The two runtimes are deliberately NOT textually identical -- `HBlock` vs `ABlk`,
`block_get` vs `blk_get`, `tycho_str_concat` vs `sc`, `stats_dump` vs `st_dump`.
Comparing identifiers would therefore need a ~150-entry spelling map that says
nothing about behaviour and rots on every rename.

So the key is the part of a runtime a USER can observe, which both sides must
spell identically because it is the contract:

  1. env knobs   -- the set of literal getenv("...") names a compiled program
                    reads. This is exactly the TYCHO_ARENA_STATS drift.
  2. diagnostics -- the set of "tycho: ..." abort/trap texts a compiled program
                    can print. A missing text == a missing check.
  3. stats rows  -- the row labels of the TYCHO_ARENA_STATS summary. The env var
                    can be present on both sides while the report it drives is
                    half-empty; that is the deeper half of the same drift.

Both sides are measured the SAME way: compile tests/rtparity/surface.ty with
each compiler and read the emitted C. That is deliberately wider than diffing
runtime/tycho_rt.c against tychoc0's string literals, because some traps are not
in the runtime file at all -- `tycho: range step is zero` is emitted inline by
the loop codegen (src/tychoc.c:9636, compiler/tychoc0.ty:8593) -- and comparing
generated C against generated C catches those too.

Because both compilers emit per-type helpers and traps ON DEMAND, the probe
program has to be broad -- see the header of surface.ty.

FAILING CLOSED
--------------
Two things keep this from decaying into a gate that cannot fail:
  * a floor count on the reference side. If extraction silently stops working
    (an --emit-c format change, a moved runtime) both sets go empty, and an
    empty set trivially matches an empty set. Below the floor, the lane fails.
  * no stale allowlist entries. An allowlisted difference that has since been
    closed is reported as a failure demanding its removal, so the allowlist can
    only ever shrink by hand.

Usage:  python3 tests/rtparity/run.py        (or `make rtparity`)
Exit 0 = the two runtimes agree; nonzero = drift (names the missing symbol).
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROBE = os.path.join(ROOT, "tests", "rtparity", "surface.ty")

# --- Intentional differences (allowlist) ------------------------------------
# tychoc0 is a SUBSET compiler: it compiles the language tychoc does, but its
# hand-written runtime skips hardening tycho_rt.c performs. Every entry below is
# a REAL, known gap -- tychoc traps, tychoc0 does not -- listed one by one and
# never as a pattern, so that
#   * a NEW divergence fails this lane instead of hiding under a category, and
#   * closing one in tychoc0 makes this lane demand the entry be deleted.
# Nothing here is a spelling difference; the comparison keys above were chosen
# so that spelling differences cannot reach this list.
ALLOW_MSG_TYCHOC_ONLY = {
    # A length taken straight from user code is range-checked before it can wrap
    # the size_t allocation: tycho_cap_check for reserve() (runtime/tycho_rt.c:190)
    # and a negative-length guard in tycho_str_alloc (runtime/tycho_rt.c:858).
    # tychoc0's Arr_*_reserve and `hs` allocate the request unchecked.
    "tycho: reserve capacity %ld out of range\\n",
    "tycho: string length %ld out of range\\n",
    # tycho_rt.c checks every malloc (runtime/tycho_rt.c:93). tychoc0's blk_get
    # does not, so an OOM there is a null deref rather than a clean abort.
    "tycho: out of memory\\n",
    # xs[a:b] is bounds-checked inline by tychoc (src/tychoc.c:8475,8494);
    # tychoc0's Arr_*_slice forwards lo/hi to Arr_*_from unchecked.
    "tycho: slice [%ld:%ld] out of bounds (len %ld)\\n",
    # The map index is int-sized, so tycho_rt.c refuses to grow past 2^31 entries
    # (runtime/tycho_rt.c:1794, :1937, :2124, :2263). tychoc0 emits its maps
    # per-type with no such guard.
    "tycho: [string:int] map exceeds 2^31 entries\\n",
    "tycho: [string:float] map exceeds 2^31 entries\\n",
    "tycho: [int:int] map exceeds 2^31 entries\\n",
    "tycho: [int:float] map exceeds 2^31 entries\\n",
}
ALLOW_MSG_TYCHOC0_ONLY = set()   # tychoc0 has never had a trap tychoc lacks
ALLOW_ENV_TYCHOC_ONLY = set()    # the env surface is identical and must stay so
ALLOW_ENV_TYCHOC0_ONLY = set()
ALLOW_ROW_TYCHOC_ONLY = set()    # the arena-stats report is identical, row for row
ALLOW_ROW_TYCHOC0_ONLY = set()

# --- Extraction --------------------------------------------------------------
# Literal getenv("NAME"). The wrapper both runtimes expose to Tycho code takes a
# runtime string (getenv(name)), so a user program's own env reads never land
# here -- only the knobs the runtime itself honours.
RE_ENV = re.compile(r'getenv\("([A-Za-z_][A-Za-z0-9_]*)"\)')
# Abort/trap texts. Every runtime diagnostic is prefixed "tycho: " by convention.
RE_MSG = re.compile(r'"(tycho: [^"]*)"')
# Arena-stats rows: `  <label>:` at the start of a report line. tychoc's copy is
# a run of adjacent C literals (label after an opening quote), tychoc0's is one
# long literal (label after an embedded \n) -- match either lead-in.
RE_ROW = re.compile(r'(?:\\n|")  ([A-Za-z][A-Za-z0-9 -]{2,}):')

# Anti-vacuity floors. If extraction ever stops working (an --emit-c format
# change, a moved runtime) both sets go empty and empty==empty passes happily.
# The floor is on the REFERENCE side only -- tychoc embeds tycho_rt.c whole, so
# its counts move only when the runtime itself does. Holding tychoc0 to a floor
# too would make every genuine one-symbol gap ALSO print "extraction is broken",
# which is exactly the wrong diagnosis. tychoc0 is instead required to be
# non-empty: nothing extracted at all can only mean the probe never ran.
FLOOR = {"env knob": 3, "diagnostic": 25, "arena-stats row": 5}


def err(msg):
    """Report to stderr, but keep it ordered against the stdout 'ok' lines --
    two independently buffered streams otherwise print the verdict before the
    findings it summarises."""
    sys.stdout.flush()
    print(msg, file=sys.stderr, flush=True)


def die(msg):
    err("rtparity: " + msg)
    sys.exit(2)


def emitted_c(tmp):
    """Compile the probe with both compilers; return (tychoc C, tychoc0 C)."""
    tychoc = os.path.join(ROOT, "tychoc")
    if not os.access(tychoc, os.X_OK):
        die("no ./tychoc -- run 'make' first")

    out_c = os.path.join(tmp, "surface_tychoc")
    r = subprocess.run([tychoc, PROBE, "--emit-c", "-o", out_c],
                       capture_output=True, text=True)
    if r.returncode != 0:
        die("tychoc could not compile the probe:\n" + r.stdout + r.stderr)

    h0 = os.path.join(tmp, "tychoc0")
    r = subprocess.run([tychoc, os.path.join(ROOT, "compiler", "tychoc0.ty"), "-o", h0],
                       capture_output=True, text=True)
    if r.returncode != 0:
        die("could not build tychoc0:\n" + r.stdout + r.stderr)

    with open(PROBE, encoding="utf-8") as f:
        r = subprocess.run([h0], stdin=f, capture_output=True, text=True)
    if r.returncode != 0:
        die("tychoc0 could not compile the probe:\n" + r.stderr)

    with open(out_c + ".c", encoding="utf-8", errors="replace") as f:
        return f.read(), r.stdout


def compare(kind, pat, c_src, h0_src, allow_c, allow_h0):
    """Diff one surface between the two emitted files. Returns a failure count."""
    c = set(pat.findall(c_src))
    h0 = set(pat.findall(h0_src))
    fails = 0

    floor = FLOOR[kind]
    for who, s, lo in (("tychoc", c, floor), ("tychoc0", h0, 1)):
        if len(s) < lo:
            err("rtparity: FAIL - only %d %s(s) extracted from %s's output (expected >= %d).\n"
                "          That is broken extraction, not drift -- an empty set would "
                "match vacuously." % (len(s), kind, who, lo))
            fails += 1

    for missing_from, present_in, diff, allow in (
            ("tychoc0", "tychoc", c - h0, allow_c),
            ("tychoc", "tychoc0", h0 - c, allow_h0)):
        for sym in sorted(diff - allow):
            err('rtparity: FAIL - %s "%s" is emitted by %s but MISSING from %s.'
                % (kind, sym, present_in, missing_from))
            fails += 1
        for stale in sorted(allow - diff):
            err('rtparity: FAIL - stale allowlist entry: %s "%s" is no longer a '
                'difference. Delete it from tests/rtparity/run.py.' % (kind, stale))
            fails += 1

    shared = len(c & h0)
    allowed = len((c - h0) & allow_c) + len((h0 - c) & allow_h0)
    if fails == 0:
        print("rtparity: %-16s %2d shared, %d allowlisted difference(s) (ok)"
              % (kind + "s", shared, allowed), flush=True)
    return fails


def main():
    tmp = tempfile.mkdtemp(prefix="rtparity.")
    try:
        c_src, h0_src = emitted_c(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    fails = 0
    fails += compare("env knob", RE_ENV, c_src, h0_src,
                     ALLOW_ENV_TYCHOC_ONLY, ALLOW_ENV_TYCHOC0_ONLY)
    fails += compare("diagnostic", RE_MSG, c_src, h0_src,
                     ALLOW_MSG_TYCHOC_ONLY, ALLOW_MSG_TYCHOC0_ONLY)
    fails += compare("arena-stats row", RE_ROW, c_src, h0_src,
                     ALLOW_ROW_TYCHOC_ONLY, ALLOW_ROW_TYCHOC0_ONLY)

    if fails:
        err("\nrtparity: FAIL - %d runtime difference(s). runtime/tycho_rt.c and the "
            "runtime\n          compiler/tychoc0.ty emits have drifted; port the change "
            "to the other\n          side, or -- if the difference is intended -- add it "
            "to the allowlist in\n          tests/rtparity/run.py WITH a reason." % fails)
        return 1
    print("rtparity: the two runtimes agree on env knobs, diagnostics and arena stats",
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
