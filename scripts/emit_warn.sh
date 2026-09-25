# The warnings EMITTED C raises in a user's build, from both compilers.
#
# Why it exists: `import "core:strings"` put two -Wunused-value warnings into
# every program built with ./tychoc on clang -- slice_bytes/slice_str's
# `_slice_ok(...) or_return` ended its statement expression on the Result(void)
# placeholder `_or.okv;` -- naming a line of a .c file the driver then deletes.
# Two probes hit it independently on 2026-09-21 (FRICTION 128). No lane could
# see it: every suite compiles with ./tychoc1 (which already emitted
# `((void)0)`), rolls its own cc line, and throws cc's stderr away unless the
# build FAILS. The same sweep found two more of the kind: math.sign's `z == z`
# at T = int (-Wtautological-compare), and tychoc1's `((k == q))` map probe
# (-Wparentheses-equality) in every int/enum-keyed map.
#
# Why named -Werror= flags and not the default set: gcc puts -Wunused-value in
# -Wall, not in its defaults, so the defect this lane was written for is
# INVISIBLE to the default set on a gcc host. Naming the flag makes it an error
# under both. -Wparentheses-equality is clang-only and gcc refuses an unknown
# -Werror=, so every candidate is probed against the host cc first and the
# active set is printed -- a lane that silently dropped a flag would read green.
#
# Why -fsyntax-only: every one of these is a front-end diagnostic; nothing past
# the parse is needed, and it keeps the lane to a few seconds.
#
# Subject: corelib/test/*/main.ty (every corelib package, instantiated by its own
# test) and tests/*.ty (the positive fixture corpus), each emitted by BOTH
# ./tychoc and ./tychoc1.
set -u
cd "$(dirname "$0")/.." || exit 2
CC="${CC:-cc}"
for c in ./tychoc ./tychoc1; do
    [ -x "$c" ] || { echo "emit-warn: no $c -- run 'make tychoc tychoc1' first"; exit 2; }
done
export TYCHO_CORELIB="$PWD/corelib"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

CANDIDATES="unused-value tautological-compare parentheses-equality"
REQUIRED="unused-value tautological-compare"   # both gcc and clang know these

printf 'int main(void) { return 0; }\n' > "$T/empty.c"
WFLAGS=""; active=""
for w in $CANDIDATES; do
    if $CC -std=c11 -fsyntax-only -Werror=$w "$T/empty.c" > /dev/null 2>&1; then
        WFLAGS="$WFLAGS -Werror=$w"; active="$active $w"
    else
        case " $REQUIRED " in
            *" $w "*) echo "emit-warn: $CC refuses -Werror=$w, which this lane requires"; exit 1 ;;
        esac
    fi
done

# The instrument is only trustworthy if it has been shown to fire, so prove it
# every run: one probe per active flag must be REFUSED, and a clean file passed.
for w in $active; do
    case "$w" in
        unused-value)         body='int f(int a) { ({ a; a; }); return a; }' ;;
        tautological-compare) body='int f(int a) { if (a == a) return 1; return 0; }' ;;
        parentheses-equality) body='int f(int a) { if ((a == 1)) return 1; return 0; }' ;;
    esac
    printf '%s\n' "$body" > "$T/probe.c"
    if $CC -std=c11 -fsyntax-only $WFLAGS "$T/probe.c" > /dev/null 2>&1; then
        echo "emit-warn: selfcheck FAILED -- -Werror=$w did not refuse its probe; the lane asserts nothing"; exit 1
    fi
done
$CC -std=c11 -fsyntax-only $WFLAGS "$T/empty.c" > /dev/null 2>&1 \
    || { echo "emit-warn: selfcheck FAILED -- a clean file was refused"; exit 1; }

# One job per (compiler, entry); xargs spreads them over 8 workers. A job
# writes a marker when it emitted C, and a report only on a finding.
cat > "$T/one.sh" <<'EOF'
c="$1"; e="$2"
id="$(printf '%s' "$c $e" | tr '/. ' '____')"
"$c" "$e" --emit-c -o "$T/$id" > /dev/null 2>&1 || exit 0   # a program that does not emit is not this lane's subject
: > "$T/$id.built"
if ! $CC -std=c11 -fwrapv -fsyntax-only $WFLAGS "$T/$id.c" > "$T/$id.log" 2>&1; then
    { echo "WARN    $c $e"
      grep -E 'error: ' "$T/$id.log" | sed "s#$T/$id.c#emitted.c#" | head -4 | sed 's/^/        /'
    } > "$T/$id.bad"
fi
rm -f "$T/$id.c"
EOF
export CC WFLAGS T
for e in corelib/test/*/main.ty tests/*.ty; do
    printf '%s %s\n' ./tychoc "$e" ./tychoc1 "$e"
done | xargs -n 2 -P 8 sh "$T/one.sh"

built=$(ls "$T" | grep -c '\.built$')
# A floor, not the count: it stops a broken glob or a compiler that emits
# nothing from leaving this lane green. 2 x (46 corelib tests + 289 fixtures) = 670 on 2026-09-24.
MIN_BUILT=600
if [ "$built" -lt "$MIN_BUILT" ]; then
    echo "emit-warn: only $built emitted programs (floor $MIN_BUILT) -- the lane asserts LESS than it claims"; exit 1
fi
nbad=$(ls "$T" | grep -c '\.bad$')
if [ "$nbad" -ne 0 ]; then
    cat "$T"/*.bad
    echo "emit-warn: FAILED ($nbad of $built emitted programs raise a warning in the user's build; checked:$active)"
    exit 1
fi
echo "emit-warn: ok ($built emitted programs from tychoc + tychoc1, zero findings under -Werror= of:$active)"
