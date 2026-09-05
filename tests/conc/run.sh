set -u
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
TYCHOC="${TYCHOC:-./tychoc1}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# LeakSanitizer is absent from Apple's ASan; detect_leaks=1 aborts every
# sanitizer binary at exit there. Gate by OS (see tests/run.sh).
case "$(uname -s)" in Darwin) TYCHO_LSAN=0 ;; *) TYCHO_LSAN=1 ;; esac
export ASAN_OPTIONS=detect_leaks=$TYCHO_LSAN
# Windows/MSYS2: mingw gcc ships no ASan/TSan runtime (docs/internals/windows-port.md phase 2
# -- TSan has no Windows-target support in gcc at all), so the asan/tsan
# variants are SKIPPED loudly and only the native leg runs. Native binaries get
# a .exe suffix (MSYS2 exec quirk with winpthread-linked PE files).
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1; EXE=".exe" ;;
    *) IS_WINDOWS=0; EXE="" ;;
esac
# Portable resource bound for the abort fixtures (esp. the spawn fork-bomb).
# GNU `timeout` and `ulimit -v` (RLIMIT_AS) are Linux-only — macOS ships
# neither. `ulimit -t` (CPU seconds) is portable; with TYCHO_MAX_TASKS pinning
# the task cap the aborts die on their own well inside it. Add a real wall-clock
# timeout only where one exists.
if [ "$IS_WINDOWS" = 1 ]; then TO="/usr/bin/timeout 15"
elif command -v timeout >/dev/null 2>&1; then TO="timeout 15"
elif command -v gtimeout >/dev/null 2>&1; then TO="gtimeout 15"
else TO=""; fi
if ( ulimit -v 1500000 ) 2>/dev/null; then AS_CAP="ulimit -v 1500000"; else AS_CAP=":"; fi
pass=0; fail=0
note() { echo "FAIL $1 ($2)"; }

for f in tests/conc/*.ty; do
    name=$(basename "$f" .ty)
    gold="tests/conc/$name.out"
    c="$TMP/$name.c"
    # -o writes the C straight into $TMP. HISTORY: this emitted with no -o and
    # then `mv "${f%.ty}.c" "$c"`, i.e. it created tests/conc/<name>.c inside the
    # tree on every run and moved it out again -- the stray-artifact behaviour
    # the loops-cleanup plan removed. `--emit-c` with no -o now writes to stdout
    # (src/tychoc.c:14734), which this line's `>/dev/null` swallowed, and all 13
    # fixtures failed with `FAIL <name> (tychoc)`.
    if ! $TYCHOC "$f" --emit-c -o "${c%.c}" >/dev/null 2>"$TMP/$name.log"; then
        note "$name" "tychoc"; sed 's/^/      /' "$TMP/$name.log"; fail=$((fail+1)); continue
    fi
    ok=1
    for variant in "-O3 -fwrapv:nat" "-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv:asan" "-fsanitize=thread -g -O1 -fwrapv:tsan"; do
        flags=${variant%:*}; tag=${variant#*:}
        if [ "$IS_WINDOWS" = 1 ] && [ "$tag" != "nat" ]; then
            echo "SKIP $name ($tag cc -- mingw gcc ships no sanitizer runtime; docs/internals/windows-port.md phase 2)"
            continue
        fi
        if ! $CC $flags -pthread -o "$TMP/$name.$tag$EXE" "$c" -lm 2>"$TMP/$name.cc"; then
            note "$name" "$tag cc"; sed 's/^/      /' "$TMP/$name.cc"; ok=0; break
        fi
        "$TMP/$name.$tag$EXE" >"$TMP/$name.got" 2>"$TMP/$name.err"; rc=$?
        # Windows/MSYS2 flake (Prism emulation): under sustained process churn
        # exec of a PE intermittently returns 127. Retry once (Windows only).
        if [ "$rc" -ne 0 ] && [ "$IS_WINDOWS" = 1 ]; then
            # Prism startup heap-corruption race (0xC0000374): retry with backoff
            for _try in 1 2; do
                sleep 2
                "$TMP/$name.$tag$EXE" >"$TMP/$name.got" 2>"$TMP/$name.err"; rc=$?
                [ "$rc" -eq 0 ] && break
            done
        fi
        if [ "$rc" -ne 0 ]; then
            note "$name" "$tag run"; sed 's/^/      /' "$TMP/$name.err"; ok=0; break
        fi
        if [ -s "$TMP/$name.err" ]; then
            note "$name" "$tag stderr"; sed 's/^/      /' "$TMP/$name.err"; ok=0; break
        fi
        if ! cmp -s "$TMP/$name.got" "$gold"; then
            note "$name" "$tag output"; diff "$gold" "$TMP/$name.got" | sed 's/^/      /'; ok=0; break
        fi
    done
    if [ $ok -eq 1 ]; then pass=$((pass+1)); else fail=$((fail+1)); fi
done

for f in tests/conc/abort/*.ty; do
    name=abort/$(basename "$f" .ty)
    want=$(cat "${f%.ty}.err")
    if ! $TYCHOC "$f" -o "$TMP/ab" >/dev/null 2>&1; then
        note "$name" "tychoc"; fail=$((fail+1)); continue
    fi
    # Bound every abort run by memory + CPU, and pin a low task cap so the
    # spawn fork-bomb (recursive spawn) hits the bounded-concurrency ceiling fast
    # instead of exhausting host threads. Harmless to the non-spawn fixtures.
    ( ulimit -t 15; $AS_CAP; TYCHO_MAX_TASKS=16 $TO "$TMP/ab$EXE" )  >/dev/null 2>"$TMP/ab.err";  rc=$?
    if [ "$rc" -eq 127 ] && [ "$IS_WINDOWS" = 1 ]; then
        sleep 1
        ( ulimit -t 15; $AS_CAP; TYCHO_MAX_TASKS=16 $TO "$TMP/ab$EXE" )  >/dev/null 2>"$TMP/ab.err";  rc=$?
    fi
    if [ $rc -eq 0 ] || ! grep -q "$want" "$TMP/ab.err"; then
        note "$name" "tychoc expected runtime die '$want'"; fail=$((fail+1))
    else
        pass=$((pass+1))
    fi
done

# Same OPT-IN `# expect:` mechanism the flat lane has (tests/run.sh:102): a
# fixture may pin WHY it is refused, as a literal substring of the diagnostic.
# Without it this loop scored the VERDICT alone -- refused for any reason at all,
# including a rule other than the one the fixture was written for, and the lane
# still printed ok. EVERY `# expect:` line is asserted, not just the first.
# The count is pinned to a literal because the mechanism is opt-in: a fixture
# that loses its line falls silently back to verdict-only scoring.
conc_exp=0
for f in tests/conc/reject/*.ty; do
    name=reject/$(basename "$f" .ty)
    grep -q '^# expect: ' "$f" && conc_exp=$((conc_exp+1))
    if $TYCHOC "$f" -o "$TMP/rej" >"$TMP/rej.log" 2>&1; then
        note "$name" "compiled but must be rejected"; fail=$((fail+1)); continue
    fi
    miss=''; sed -n 's/^# expect: //p' "$f" > "$TMP/rej.exp"
    while IFS= read -r exp; do
        if [ -n "$exp" ] && ! grep -qF -- "$exp" "$TMP/rej.log"; then miss="$exp"; break; fi
    done < "$TMP/rej.exp"
    if [ -n "$miss" ]; then
        note "$name" "diagnostic does not contain the expected text: $miss"
        head -3 "$TMP/rej.log" | sed 's/^/      /'
        fail=$((fail+1)); continue
    fi
    pass=$((pass+1))
done
# The six R21c ported: chan_reassign, chan_sendmis, closure, builtin, inout,
# wait_nontask -- each carrying tychoc1's whole message. Plus discard.ty, which
# had carried an `# expect:` line since it was written: with no mechanism to read
# it, it was a comment, and the rule it names was scored by verdict alone.
if [ "$conc_exp" -eq 7 ]; then
    pass=$((pass+1))
else
    note "reject/expect-count" "$conc_exp fixtures carry an '# expect:' line, expected 7"
    fail=$((fail+1))
fi

# `parallel(W)` must reach CODEGEN, which no output can show. With no way to
# observe a worker's identity from inside the loop (FRICTION open-list item 8b),
# a `parallel(3)` that quietly ran at ncpu() prints exactly what a real one
# prints -- tests/conc/parfor_width.ty says so in its own header. So the emitted
# `_pk` initialiser is read here: the literal widths must appear as themselves
# and the un-widthed form must still be ncpu(). Verified to redden 2026-08-13 by
# ignoring s->par_width in gen_parfor.
$TYCHOC tests/conc/parfor_width.ty --emit-c -o "$TMP/pw" >/dev/null 2>&1
if [ -f "$TMP/pw.c" ]; then
    if grep -q "_pk = 1;" "$TMP/pw.c" && grep -q "_pk = 3;" "$TMP/pw.c" && grep -q "_pk = tycho_ncpu();" "$TMP/pw.c"; then
        pass=$((pass+1))
    else
        note "parfor_width/codegen" "the emitted _pk does not carry 1, 3 and ncpu()"; fail=$((fail+1))
    fi
else
    note "parfor_width/codegen" "no emitted C to read"; fail=$((fail+1))
fi

echo "conc: passed $pass   failed $fail"
[ $fail -eq 0 ] || exit 1
