#!/bin/sh
# corelib test harness. For each corelib/test/<name>/main.ty: compile with tychoc and
# assert the program's output matches the golden corelib/test/<name>.out. Sets
# TYCHO_CORELIB.
#
# Until 2026-07-26 each test was ALSO built two more ways -- the self-hosted tychoc0 fed
# `tychoc --bundle`, and standalone tychoc0 resolving `import "core:X"` itself -- with all
# three outputs required to match. tychoc0 is frozen (see compiler/tychoc0.ty) and no gate
# builds it, so those two legs are gone. The golden comparison, the dependency skip, and
# every assertion about tychoc are unchanged; what was lost is the second implementation
# as a cross-check on corelib behaviour.
set -u
cd "$(dirname "$0")/.." || exit 2                      # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
for entry in corelib/test/*/main.ty; do
    [ -e "$entry" ] || continue
    name="$(basename "$(dirname "$entry")")"
    # core:signal's test was SKIPPED here on Windows until 2026-08-09 -- its
    # mechanism was `kill -TERM $PPID`, and MSYS2's kill terminates a native PE
    # rather than signalling it, so the handler never fired and the accept
    # blocked forever (the phase-4 documented hang). The test now picks its
    # delivery per platform and raises a real CTRL_BREAK on a private console
    # on Windows; its output there is byte-identical to the Linux golden, so it
    # needs no `.win` sibling and no skip.
    golden="corelib/test/$name.out"
    # Windows-aware golden, the same `<golden>.win` convention tests/run.sh:157
    # already uses. Only ever selected on Windows, and only when the sibling
    # exists -- a package with one platform-independent output keeps one golden.
    # core:os is the first user: exec/exec_out are POSIX-only and fail closed
    # with -1 there (os_shim.c's `gap:` note), and cmd.exe renders the shell
    # contrast line differently from /bin/sh.
    if [ "$(uname -s | grep -ciE 'MSYS|MINGW|CYGWIN')" -ne 0 ] && [ -f "corelib/test/$name.out.win" ]; then
        golden="corelib/test/$name.out.win"
    fi
    # FFI SKIP: tychoc auto-discovers each imported module's <mod>_shim.c and its
    # `deps`, and links both itself -- the build below is a plain `tychoc -o`. So
    # this lane needs no shim list and no cc flags. The one thing it does need is
    # whether the link line CAN resolve on this host, so that a test whose
    # dependency is absent is SKIPPED rather than failed (that keeps `make ci`
    # green on a platform without the lib). ASK THE COMPILER: `--print-deps`
    # prints the pkg-config NAMES of the whole transitive import closure, one per
    # line, from the same `merge_pkg` walk `--print-shims` reads -- and it prints
    # them whether or not they resolve here, which is the point: on the host that
    # is missing the library the resolved flags are empty, and only the names can
    # say what to skip for.
    #
    # THIS WAS A GREP over the program's own `import` lines until 2026-08-02, and
    # a grep sees DIRECT imports only. That is the same transitive blindness
    # `--print-shims` was added to fix in examples/fetch/run.sh, whose header
    # records two silent staleness breaks caused by it. Measured on this tree
    # before the change: over all 68 programs in this lane and the corelib
    # examples lane the grep and the flag agree, because every deps-bearing
    # module (compress, crypto, http, image, tls) is imported directly by its own
    # test -- so the blindness here is LATENT, not live. The first module to
    # reach zlib or libcurl through an intermediate would have skipped nothing and
    # failed to link instead, which is a red on a host the SKIP exists to spare.
    #
    # The old loop also built a `shim` and a `depflags` that NOTHING in this file
    # read; they were dead from the day tychoc learned to discover them. Deleted.
    if ! allpkgs="$("$TYCHOC" "$entry" --print-deps 2>"$T/deps.log")"; then
        echo "FAIL $name (tychoc --print-deps)"; sed 's/^/      /' "$T/deps.log"; fail=1; continue
    fi
    if [ -n "$allpkgs" ]; then
        missing=""
        for pkg in $allpkgs; do pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"; done
        if [ -n "$missing" ]; then echo "skip $name (missing dependency:$missing)"; continue; fi
    fi
    if ! "$TYCHOC" "$entry" -o "$T/c" >/dev/null 2>&1; then echo "FAIL $name (tychoc compile)"; fail=1; continue; fi
    "$T/c" > "$T/co" 2>&1; rc=$?
    # Windows/MSYS2 flake (Prism emulation): under sustained process churn exec
    # of a PE intermittently returns 127. Retry with backoff (Windows only); the
    # Prism startup heap-corruption race (0xC0000374) is per-attempt.
    if [ "$rc" -eq 127 ] && [ "$(uname -s | grep -ciE 'MSYS|MINGW|CYGWIN')" -ne 0 ]; then
        for _try in 1 2 3; do
            sleep 2
            "$T/c" > "$T/co" 2>&1; rc=$?
            [ "$rc" -eq 0 ] && break
        done
    fi
    if [ "$RECORD" = 1 ]; then cp "$T/co" "$golden"; echo "rec  $name"; continue; fi
    if [ ! -f "$golden" ]; then echo "FAIL $name (no golden -- run RECORD=1)"; fail=1; continue; fi
    if ! cmp -s "$T/co" "$golden"; then echo "FAIL $name (output != golden)"; diff "$golden" "$T/co" | head | sed 's/^/      /'; fail=1; continue; fi
    echo "ok   $name"
done
[ "$fail" -eq 0 ] && echo "corelib: all green (tychoc matches goldens)" || { echo "corelib: FAIL"; exit 1; }
