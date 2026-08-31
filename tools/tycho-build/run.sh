set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
D=tools/tycho-build
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
RECORD="${RECORD:-0}"
fail=0
# Leg [7] shells out three ways that are POSIX-shaped: a file:// URL built from
# $PWD, a buildfile recipe that names the compiler by path, and an output binary
# with no suffix. Under MSYS2 $PWD is the POSIX view (/c/...), which libcurl -- a
# native DLL -- cannot open; the recipe reaches cmd.exe, which cannot execute a
# name written with forward slashes; and the linker appends .exe. cygpath -m
# gives the mixed form (C:/...) that a file: URL and the CRT both accept, and
# cygpath -w the backslash form cmd needs to START a program.
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1; EXE=".exe" ;; *) IS_WINDOWS=0; EXE="" ;; esac
url_of() { if [ "$IS_WINDOWS" = 1 ]; then echo "file:///$(cygpath -m "$1")"; else echo "file://$1"; fi; }
native() { if [ "$IS_WINDOWS" = 1 ]; then cygpath -w "$1"; else echo "$1"; fi; }

"$TYCHOC" "$D/main.ty" -o "$T/tb" >"$T/build.log" 2>&1 || { echo "FAIL (tool build)"; cat "$T/build.log"; exit 2; }

# the fixture: a chain (src -> out1, out2 -> final) plus an independent branch
fix() {                                                   # $1 = fixture dir
    mkdir -p "$1"
    cat > "$1/buildfile" <<'EOF'
all: final other
final: out1 out2
    cat out1 out2 > final
out1: src
    cp src out1
out2: src
    cp src out2
other: other_src
    cp other_src other
EOF
    printf 'SOURCE-ONE\n' > "$1/src"
    printf 'OTHER-SRC\n' > "$1/other_src"
}

# [1] first build: golden dispatch order, correct outputs, exit 0
fix "$T/a"
( cd "$T/a" && "$T/tb" buildfile ) > "$T/a1.out" 2> "$T/a1.err"; rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL [1] first build exit $rc"; cat "$T/a1.err"; fail=1; }
if [ "$RECORD" = 1 ]; then
    cp "$T/a1.out" "$D/expected.out"; echo "rec  [1] first-build golden"
elif ! cmp -s "$T/a1.out" "$D/expected.out"; then
    echo "FAIL [1] first-build stdout != golden"; diff "$D/expected.out" "$T/a1.out" | head; fail=1
fi
[ -f "$T/a/out1" ] && [ -f "$T/a/out2" ] && [ -f "$T/a/other" ] && [ -f "$T/a/final" ] \
    || { echo "FAIL [1] outputs missing"; fail=1; }
[ "$(cat "$T/a/out1")" = "SOURCE-ONE" ] && [ "$(cat "$T/a/final")" = "SOURCE-ONE
SOURCE-ONE" ] && [ "$(cat "$T/a/other")" = "OTHER-SRC" ] \
    || { echo "FAIL [1] output contents wrong"; fail=1; }

# [2] second build is a NO-OP: empty stdout, exit 0
( cd "$T/a" && "$T/tb" buildfile ) > "$T/a2.out" 2> "$T/a2.err"; rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL [2] no-op build exit $rc"; cat "$T/a2.err"; fail=1; }
[ -s "$T/a2.out" ] && { echo "FAIL [2] no-op build printed:"; cat "$T/a2.out"; fail=1; }

# [3] touch src -> only its dependents rebuild (out1, out2, final; not other)
sleep 1; touch "$T/a/src"
( cd "$T/a" && "$T/tb" buildfile ) > "$T/a3.out" 2> "$T/a3.err"; rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL [3] rebuild exit $rc"; cat "$T/a3.err"; fail=1; }
exp3="build out1
build out2
build final"
printf '%s\n' "$exp3" > "$T/exp3"
[ "$(cat "$T/a3.out")" = "$exp3" ] || { echo "FAIL [3] rebuild lines wrong"; diff "$T/exp3" "$T/a3.out" | head; fail=1; }

# [4] a failing recipe fails the build and skips its dependents
mkdir -p "$T/f"
cat > "$T/f/buildfile" <<'EOF'
all: bad dep
bad: src
    exit 3
dep: bad
    printf built > dep_out
EOF
printf 'S\n' > "$T/f/src"
( cd "$T/f" && "$T/tb" buildfile ) > "$T/f.out" 2> "$T/f.err"; rc=$?
[ "$rc" -eq 1 ] || { echo "FAIL [4] failing build exit $rc (want 1)"; fail=1; }
grep -q "build bad" "$T/f.out" || { echo "FAIL [4] no 'build bad' line"; fail=1; }
grep -q "FAILED bad (exit 3)" "$T/f.out" || { echo "FAIL [4] no FAILED line"; cat "$T/f.out"; fail=1; }
grep -q "build dep" "$T/f.out" && { echo "FAIL [4] dependent ran after failure"; fail=1; }
[ -f "$T/f/dep_out" ] && { echo "FAIL [4] dependent output exists"; fail=1; }

# [5] determinism: two clean fixtures build byte-identically
fix "$T/c1"; fix "$T/c2"
( cd "$T/c1" && "$T/tb" buildfile ) > "$T/c1.out" 2>/dev/null
( cd "$T/c2" && "$T/tb" buildfile ) > "$T/c2.out" 2>/dev/null
cmp -s "$T/c1.out" "$T/c2.out" || { echo "FAIL [5] builds differ"; diff "$T/c1.out" "$T/c2.out" | head; fail=1; }

# [6] usage / parse / io errors exit 2
"$T/tb" "$T/nonexistent" >/dev/null 2>&1; [ $? -eq 2 ] || { echo "FAIL [6] missing buildfile"; fail=1; }
printf 'garbage line\n' > "$T/bad"
"$T/tb" "$T/bad" >/dev/null 2>&1;   [ $? -eq 2 ] || { echo "FAIL [6] parse error"; fail=1; }
( cd "$T/a" && "$T/tb" buildfile nosuch ) >/dev/null 2>&1; [ $? -eq 2 ] || { echo "FAIL [6] unknown target"; fail=1; }
printf 'a: b\nb: a\n    touch a\n' > "$T/cyc"
"$T/tb" "$T/cyc" >/dev/null 2>&1;   [ $? -eq 2 ] || { echo "FAIL [6] cycle"; fail=1; }

# [7] vendored dependencies through the build tool: fetch two packages with
# tycho-fetch (local file:// tarballs), then build a project whose entry
# imports "vendor/greet", which itself imports "../util" -- the whole
# Go/Odin-style vendoring story end to end.
if ! "$TYCHOC" "$D/../tycho-fetch/main.ty" --shim tools/tycho_shim.c -o "$T/tf" >"$T/tf.log" 2>&1; then
    echo "FAIL [7] tycho-fetch build"; cat "$T/tf.log"; fail=1
fi
mkdir -p "$T/vend/g-src" "$T/vend/u-src"
printf 'package greet\nimport "../util"\nfn hello():\n    print(util.msg() + "\\n")\n' > "$T/vend/g-src/greet.ty"
printf 'package util\nfn msg() -> string:\n    return "from vendored util"\n' > "$T/vend/u-src/util.ty"
( cd "$T/vend" && tar -czf g.tar.gz g-src && tar -czf u.tar.gz u-src )
printf 'package main\nimport "vendor/greet"\nfn main():\n    greet.hello()\n' > "$T/vend/main.ty"
printf 'app: main.ty\n    %s main.ty -o app%s\n' "$(native "$PWD/tychoc")" "$EXE" > "$T/vend/buildfile"
( cd "$T/vend" && "$T/tf" "$(url_of "$PWD/g.tar.gz")" greet && "$T/tf" "$(url_of "$PWD/u.tar.gz")" util ) > "$T/vend/fetch.log" 2>&1 \
    || { echo "FAIL [7] tycho-fetch"; cat "$T/vend/fetch.log"; fail=1; }
[ -f "$T/vend/vendor/greet/greet.ty" ] && [ -f "$T/vend/vendor/util/util.ty" ] \
    || { echo "FAIL [7] vendored files missing"; fail=1; }
( cd "$T/vend" && "$T/tb" buildfile ) > "$T/vend/build.out" 2>&1 \
    || { echo "FAIL [7] vendored build"; cat "$T/vend/build.out"; fail=1; }
[ -x "$T/vend/app$EXE" ] || { echo "FAIL [7] app not built"; fail=1; }
out=$("$T/vend/app$EXE")
[ "$out" = "from vendored util" ] || { echo "FAIL [7] app output: '$out'"; fail=1; }

[ "$fail" -eq 0 ] && echo "tycho-build: all green (7 legs)" || { echo "tycho-build: FAIL"; exit 1; }
