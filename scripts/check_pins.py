#!/usr/bin/env python3
"""Assert the code properties that no suite covers.

These came out of a defect log (deleted 2026-09-20) that carried 198 `Pinned-by:`
lines wrapped in 8,382 lines of narrative. Scoring them found that 31 pins
re-ran lanes `make ci` already runs and ~40 more were `test -f` on files
corpus-check, goldens-check and `make corelib` already require. What was left
is here: each one is a fix that a test cannot see, because the property is a
compiler flag, a shim call, a retry count or a lane's presence in the sweep.

A pin is a shell command; exit 0 means the property still holds. The reason is
not decoration -- it is the only thing that tells a reader why deleting the
line would be wrong.
"""

import concurrent.futures
import subprocess
import sys

# (reason, command). Reason is one line, present tense, naming what breaks.
PINS = [
    # Float determinism. Both compilers must pin the contraction, or the same
    # source fuses a multiply-add on one and not the other.
    ("tychoc pins float contraction",
     "grep -q 'ffp-contract=off' src/tychoc.c"),
    ("the self-hosted driver pins float contraction",
     "grep -q 'ffp-contract=off' compiler/driver/driver.ty"),

    # A qualified builtin -- strings.len(...) -- must be recognised as a
    # builtin before sig_find() is asked, which has no entry for a generic one.
    ("tychoc routes qualified builtins before sig_find",
     "grep -q 'is_builtin_name(nominal_name' src/tychoc.c"),

    # Shim-level fixes. Each is a one-line call whose absence is silent.
    ("net listens with a real backlog",
     "grep -q 'listen((int)fd, SOMAXCONN)' corelib/net/net_shim.c"),
    ("tls rejects an embedded NUL in the host",
     "grep -q '_has_nul(host)' corelib/tls/tls.ty"),
    ("the os shim marks its unused helpers",
     "grep -q '__attribute__((unused))' corelib/os/os_shim.c"),
    ("uuid documents itself as NOT unguessable",
     "grep -q 'NOT unguessable' corelib/uuid/uuid.ty"),
    ("bignum keeps the magnitude comparison it was fixed with",
     "grep -q '^fn _mag_cmp' corelib/bignum/bignum.ty"),

    # Runners. A flaky lane was made deterministic by a retry or a hard kill;
    # both revert to a hang under a plain edit.
    ("tycho-make retries its race probe",
     "grep -q '_race_attempts=3' tools/tycho-make/run.sh"),
    ("tycho-kvsrv kills its server rather than waiting on it",
     "grep -q 'kill -KILL' tools/tycho-kvsrv/run.sh"),
    ("no tool runner hardcodes an absolute TYCHOC",
     'test -z "$(grep -lE \'^TYCHOC=.\\$PWD/tychoc.\' tools/*/run.sh)"'),

    # Overflow fixes in the two examples that parse untrusted numbers.
    ("the site example parses its draw count checked",
     "grep -q 'parse_int_checked(draw)' examples/site/main.ty"),
    ("the weblog example parses its byte count checked",
     "grep -q 'strings.parse_int_checked(bytes_s)' examples/weblog/main.ty"),
    ("the weblog example does not parse bytes unchecked",
     "! grep -q 'nbytes := strings.parse_int(bytes_s)' examples/weblog/main.ty"),

    # Two suites were made to assert on elapsed time rather than a loop count,
    # which is what made them pass on a fast box while the behaviour was gone.
    ("the net test waits on a stopwatch",
     "grep -q 'time.elapsed_ms(sw) >= 5000' corelib/test/net/main.ty"),
    ("the net test does not spin a fixed 50 times",
     "! grep -qE '^ +for t := 0; t < 50; t \\+= 1:' corelib/test/net/main.ty"),
    ("the httpd test reads the second chunk it asserts on",
     "grep -q 'chunk2 := to_str(data_of(net.read(cli2, 4096)))' corelib/test/httpd/main.ty"),

    # The release archive is the one artefact no lane inspects by default.
    ("release-content checks a native Darwin build",
     "grep -q 'check_native_darwin' scripts/release_content.sh"),
    ("release-content can skip mingw rather than fail on its absence",
     "grep -q 'mingw_skip=' scripts/release_content.sh"),
    ("the constant-time probe builds its control at -O0",
     "grep -q 'ct_run \"$T/ctl\" -O0' scripts/crypto_hygiene.sh"),

    # The freeze. A package-private name in surface.lock is a name that leaked.
    ("no package-private name is in the freeze",
     "python3 -c \"import json;d=json.load(open('surface.lock'));"
     "assert not [k for k in d['corelib'] if '._' in k], 'a package-private name is back in the freeze'\""),

    # Makefile prerequisites. Both of these shipped as an order-dependent green.
    ("docs-fences declares the tools its fences run",
     "grep -qE '^docs-fences: tychoc1 tycho$' Makefile"),
    ("builtin-qualified declares both compilers",
     "grep -q '^builtin-qualified: tychoc tychoc1' Makefile"),

    # A lane nothing calls is a lane that does not exist. These two were added
    # to the sweep after each spent a day red with every commit recording green.
    ("ci runs corpus-check", "grep -q 'make -s corpus-check' scripts/ci.sh"),
    ("ci runs parity-fuzz", "grep -q 'make -s parity-fuzz' scripts/ci.sh"),
]


def run(cmd):
    r = subprocess.run(["sh", "-c", cmd], capture_output=True, text=True)
    tail = (r.stdout + r.stderr).strip().split("\n")[-1][:100] if r.returncode else ""
    return r.returncode, tail


def selfcheck():
    """A gate that cannot fail is not a gate. Prove both verdicts, and prove
    every row is well-formed -- a pin with an empty reason is unreviewable."""
    ok = True

    def leg(name, got, want):
        nonlocal ok
        if got != want:
            ok = False
        print("  %-46s %s (got %r)" % (name, "ok" if got == want else "FAILED", got))

    leg("[1] a holding pin exits zero", run("true")[0], 0)
    leg("[2] a broken pin exits non-zero", run("false")[0] != 0, True)
    leg("[3] a broken pin is reported with its tail",
        run("echo nope >&2; false")[1], "nope")
    leg("[4] every pin carries a reason", [r for r, _ in PINS if not r.strip()], [])
    leg("[5] every pin carries a command", [c for _, c in PINS if not c.strip()], [])
    leg("[6] no pin is listed twice", len({c for _, c in PINS}), len(PINS))
    # The point of the extraction: no pin here may re-run a lane the sweep runs.
    suites = [c for _, c in PINS if c.startswith(("make ", "sh scripts/"))]
    leg("[7] no pin re-runs a suite", suites, [])

    print("pin selfcheck: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    if "--selfcheck" in sys.argv:
        return selfcheck()
    bad = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        results = list(ex.map(run, [c for _, c in PINS]))
    for (reason, cmd), (rc, tail) in zip(PINS, results):
        if rc:
            bad += 1
            print("STALE  %s\n         `%s` exited %d: %s" % (reason, cmd, rc, tail))
    print("pin check: %s (%d code properties asserted)"
          % ("FAILED" if bad else "ok", len(PINS)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
