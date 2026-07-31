#!/bin/sh
# shim_check.sh -- every corelib `<pkg>_shim.c` must compile ON ITS OWN.
#
# WHY THIS EXISTS. A shim is never compiled alone by the real build: tychoc
# appends it to the generated .c on one cc line (src/tychoc.c@add_shim feeds the
# `shims` slot of the command built at the end of main). So a shim that only
# compiles because something earlier in that line already pulled in a header, or
# because the compiler's default dialect happened to expose a POSIX symbol, looks
# fine forever. FRICTION.md carried exactly that defect as open across two
# re-scores, and while it was open corelib/signal/signal_shim.c was written and
# reproduced it -- because nothing in the tree ever compiled a shim on its own.
# This does.
#
# THE FLAGS, AND HOW THEY RELATE TO THE REAL BUILD. tychoc emits
#     cc -O3 -fwrapv -pthread -o <out> <generated.c> <shims...> -lm ... <pkgdeps>
# (src/tychoc.c@main, the sfmt that feeds `system(cmd)`). Three deliberate
# differences, each with a reason:
#
#   * `-std=c11` is ADDED, and it is the entire point. The build passes no -std
#     at all, so cc uses its default gnu dialect, which does not define
#     __STRICT_ANSI__ -- and glibc's <features.h> therefore turns _DEFAULT_SOURCE
#     on by itself. That is why getaddrinfo and sigaction resolve in the build
#     while the shim declares no feature-test macro. -std=c11 sets
#     __STRICT_ANSI__, the implicit _DEFAULT_SOURCE goes away, and the shim has
#     to say what it needs. The gate is STRICTER than the build on purpose: it
#     asks the shim to carry its own requirements rather than inherit the host
#     compiler's default dialect.
#   * `-fsyntax-only` replaces -O3/-fwrapv/-o. Nothing here links, and -fwrapv is
#     a codegen contract with no bearing on whether the source parses.
#   * `-pthread` is DROPPED, and this one was learned the hard way -- the first
#     version of this gate kept it for build fidelity and was worthless. -pthread
#     defines _REENTRANT, and glibc's system <features.h> treats _REENTRANT as a
#     compatibility synonym raising _POSIX_C_SOURCE to 199506L (the guard block
#     commented "compatibility synonyms for _POSIX_C_SOURCE=199506L"). That hands
#     back the POSIX declarations -std=c11 had just withheld. Measured on the
#     unfixed corelib/signal/signal_shim.c: `cc -std=c11 -fsyntax-only` gives 3
#     errors, `cc -std=c11 -pthread -fsyntax-only` gives 0. With -pthread this
#     gate passes all twelve shims unfixed -- green, and testing nothing, which
#     is exactly the failure mode it was written to rule out. Build fidelity
#     loses to strictness here: -pthread is the one flag that decides whether
#     there is a gate at all.
#   * The pkg-config cflags from `<dir>/deps` are KEPT, because dropping them is
#     not strictness, it is a false failure. corelib/image/image_shim.c reports
#     `png.h: No such file or directory` without them -- a statement about which
#     -I flags were passed, not about the shim. When the package is absent from
#     the host entirely the shim is SKIPPED, matching corelib/run.sh:39 and what
#     every `deps` file's own header promises ("the corelib harness skips this
#     module test when the dependency is absent, so `make ci` stays green").
#
# NOT passed: -Iruntime. No shim includes a project header -- `grep '#include "'
# corelib/*/*_shim.c` is empty, they are angle-bracket includes only. Adding the
# project's own include path would be worse than useless: the isolation this gate
# exists to test is precisely that a shim needs nothing from the tree.
set -eu

CC="${CC:-cc}"
fail=0 ok=0 skipped=0

for shim in corelib/*/*_shim.c; do
    dir="$(dirname "$shim")"
    depflags=""

    if [ -f "$dir/deps" ]; then
        pkgs="$(grep -vE '^[[:space:]]*(#|$)' "$dir/deps" || true)"
        missing=""
        for pkg in $pkgs; do
            pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"
        done
        if [ -n "$missing" ]; then
            echo "skip $shim (missing dependency:$missing)"
            skipped=$((skipped + 1))
            continue
        fi
        depflags="$(pkg-config --cflags $pkgs 2>/dev/null || true)"
    fi

    if out="$($CC -std=c11 -fsyntax-only $depflags "$shim" 2>&1)"; then
        echo "ok   $shim"
        ok=$((ok + 1))
    else
        echo "FAIL $shim"
        echo "$out" | sed 's/^/       /'
        fail=$((fail + 1))
    fi
done

echo "shim-check: $ok ok, $skipped skipped, $fail failed"
[ "$fail" -eq 0 ] || {
    echo "shim-check: a shim does not compile standalone under -std=c11." >&2
    echo "  Fix it in the shim, not here: declare the feature-test macro it needs" >&2
    echo "  before its first #include, the way corelib/io/io_shim.c does." >&2
    exit 1
}
