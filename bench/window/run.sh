set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
D=bench/window
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }
# ru_maxrss is KB on Linux, bytes on macOS/BSD — normalize to KB, then /1024 for MB.
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
ref=""; FAIL=0
meas() {  # <label> <binary> <check?>
    "$T/peakrss" "$2" > "$T/o" 2> "$T/m"
    kb=$(to_kb "$(awk '{print $1}' "$T/m")"); rss=$(awk "BEGIN{printf \"%.1f\", $kb/1024}"); ms=$(awk '{print $2}' "$T/m"); out=$(cat "$T/o")
    printf '  %-14s %7s MB %6s ms   %s\n' "$1" "$rss" "$ms" "$out"
    if [ "$3" = check ]; then
        if [ -z "$ref" ]; then ref="$out"; elif [ "$out" != "$ref" ]; then echo "    ^ CHECKSUM MISMATCH"; FAIL=1; fi
    fi
}
echo "sliding window of HEAP records (string) — 2M stream, 50k window:"
$TYCHOC "$D/window_naive.ty" --emit-c -o "$T/wn" >/dev/null 2>&1 && $CC -O3 -o "$T/wn" "$T/wn.c" -lm; meas "tycho (string)" "$T/wn" check
$CC -O3 -o "$T/wc" "$D/window.c"; meas "C (string)" "$T/wc" check
if command -v go >/dev/null 2>&1; then cp "$D/window.go" "$T/wg.go" && ( cd "$T" && go build -o wgo wg.go 2>/dev/null ); meas "Go (string)" "$T/wgo" check; fi
if command -v rustc >/dev/null 2>&1; then rustc -C opt-level=3 -o "$T/wr" "$D/window.rs" 2>/dev/null && meas "Rust (string)" "$T/wr" check; fi
if command -v javac >/dev/null 2>&1; then mkdir -p "$T/jc"; javac -d "$T/jc" "$D/window.java" 2>/dev/null && printf '#!/bin/sh\nexec java -cp "%s" window\n' "$T/jc" > "$T/jw" && chmod +x "$T/jw" && meas "Java (string)" "$T/jw" check; fi
if command -v swiftc >/dev/null 2>&1; then swiftc -O -o "$T/ws" "$D/window.swift" 2>/dev/null && meas "Swift (string)" "$T/ws" check; fi
echo "same window with FIXED-SIZE records (int) — slots reused, no per-element heap:"
ref=""
$TYCHOC "$D/window_int.ty" --emit-c -o "$T/wi" >/dev/null 2>&1 && $CC -O3 -o "$T/wi" "$T/wi.c" -lm; meas "tycho (int)" "$T/wi" check
$CC -O3 -o "$T/wic" "$D/window_int.c"; meas "C (int)" "$T/wic" check
if command -v go >/dev/null 2>&1; then cp "$D/window_int.go" "$T/wig.go" && ( cd "$T" && go build -o wigo wig.go 2>/dev/null ); meas "Go (int)" "$T/wigo" check; fi
if command -v rustc >/dev/null 2>&1; then rustc -C opt-level=3 -o "$T/wir" "$D/window_int.rs" 2>/dev/null && meas "Rust (int)" "$T/wir" check; fi
if command -v javac >/dev/null 2>&1; then mkdir -p "$T/jci"; javac -d "$T/jci" "$D/window_int.java" 2>/dev/null && printf '#!/bin/sh\nexec java -cp "%s" window_int\n' "$T/jci" > "$T/jwi" && chmod +x "$T/jwi" && meas "Java (int)" "$T/jwi" check; fi
if command -v swiftc >/dev/null 2>&1; then swiftc -O -o "$T/wis" "$D/window_int.swift" 2>/dev/null && meas "Swift (int)" "$T/wis" check; fi
[ "$FAIL" = 0 ] && echo "window: ok (string and int checksums agree)" || { echo "window: FAIL"; exit 1; }
