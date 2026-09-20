#!/usr/bin/env python3
"""Every PUBLIC corelib function is named in the catalogue.

FRICTION 99: `core:net`'s entry in docs/reference/corelib.md told readers the
package had no readiness polling, in a package whose shim calls poll(2) eight
times and whose `wait_readable` has its own CI lane. 41 of 431 public functions
were absent from the catalogue entirely, and the crypto key lifecycle -- the
handle `aead_encrypt` takes -- was among them, so the package could not be used
correctly from the reference alone.

Nothing caught it because nothing could. `check-links` verifies links and
citations, `version-check` verifies STATUS claims, `docs-fences` compiles fenced
code. None of them compares a claim about a package against that package. The
catalogue is the one document whose subject IS an inventory, which is exactly
where that drift is invisible: adding a function to net.ty reddened nothing.

This is the cheap half of the fix -- prose cannot be checked, but ABSENCE can.
A public function the catalogue never names is undocumented by definition, and
that is the state 99 was found in. Private functions (leading underscore) are
excluded: the resolver refuses them from another package, so they are not API.

Costs one file read and a substring scan of ~430 names.
"""
import io
import json
import re
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(ROOT, "surface.lock")
CAT = os.path.join(ROOT, "docs", "reference", "corelib.md")

# Names the catalogue documents as a FAMILY rather than one at a time -- the
# entry reads `ed25519_pubkey`/`sign`/`verify`, which a reader resolves but a
# substring scan does not. Each needs the family's stem present in the text.
#
# WORD-BOUNDARY, not a bare substring, and the difference is 11 functions.
# This gate shipped on 2026-09-19 doing `if name in cat`, and its own commit
# message called it "a substring scan of ~430 names" -- the weakness was
# written down and its consequence was never measured. A short name passes on
# any word that contains it: `place` inside *replace*, `ch` inside *search*,
# `esc` inside *escape*, `asc`/`desc` inside *ascending*/*descending*, `bad`
# inside *bad request*. So `431/431` meant 420 genuinely named and 11 not
# documented at all -- a gate reporting complete coverage of an inventory it
# had not actually checked, which is the exact failure FRICTION 99 created it
# to prevent (FRICTION 118).
FAMILY = {
    "ed25519_sign": "ed25519_pubkey",
    "ed25519_verify": "ed25519_pubkey",
}


def named(name, cat):
    """Is `name` actually NAMED in the catalogue text, as a word?

    One definition, used by the scan and by selfcheck alike -- the two matching
    each other is the only reason a passing selfcheck says anything about a
    passing scan. Until 2026-09-19 both used `name in cat`, a bare substring,
    and so did the selfcheck that was supposed to police it (FRICTION 118)."""
    return re.search(r"\b%s\b" % re.escape(name), cat) is not None


def selfcheck():
    """The scan must be able to FAIL: a name that is not in the catalogue must
    be reported, or a clean run means only that the scan is not looking."""
    cat = "documented: alpha, beta. Also: replace, search, escape, ascending."
    legs = [
        ("[1] a documented name passes",            named("alpha", cat),  True),
        ("[2] an absent name is reported",          named("gamma", cat),  False),
        # The legs that matter. Every one of these passed the old substring scan
        # by hiding inside a longer word, and each is a REAL function name that
        # was reported as documented while the catalogue never mentioned it.
        ("[3] `place` does NOT match *replace*",    named("place", cat),  False),
        ("[4] `ch` does NOT match *search*",        named("ch", cat),     False),
        ("[5] `esc` does NOT match *escape*",       named("esc", cat),    False),
        ("[6] `asc` does NOT match *ascending*",    named("asc", cat),    False),
        ("[7] a name at a word boundary still matches",
                                                    named("search", cat), True),
    ]
    bad = 0
    for label, got, want in legs:
        good = got == want
        bad += 0 if good else 1
        print("  %-44s %s" % (label, "ok" if good else "BROKEN (got %r)" % got))
    if bad:
        print("corelib-doc selfcheck: FAILED (%d leg(s) do not discriminate)" % bad)
        return 1
    print("corelib-doc selfcheck: ok (a documented name passes, an absent one is "
          "reported, and a short name does not pass by hiding inside a longer word)")
    return 0


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    if not os.path.exists(LOCK):
        print("corelib-doc: no surface.lock -- run surface_lock.py --record")
        return 2
    fns = json.load(io.open(LOCK, encoding="utf-8"))["corelib"]
    cat = io.open(CAT, encoding="utf-8").read()
    missing = []
    for full in sorted(fns):
        name = full.split(".", 1)[1]
        if name.startswith("_"):
            continue                      # package-private: not API
        if named(name, cat):
            continue
        stem = FAMILY.get(name)
        if stem and named(stem, cat):
            continue
        missing.append(full)
    if missing:
        for m in missing:
            print("corelib-doc: UNDOCUMENTED  %s (%s)" % (m, fns[m]))
        print("corelib-doc: FAILED (%d public function(s) the catalogue never names -- "
              "document them in docs/reference/corelib.md, or make them "
              "package-private with a leading underscore)" % len(missing))
        return 1
    print("corelib-doc: ok (%d public corelib functions, every one named in the catalogue)"
          % sum(1 for f in fns if not f.split(".", 1)[1].startswith("_")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
