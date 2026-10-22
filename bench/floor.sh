#!/bin/sh
# Startup floor: what an empty program costs per implementation.
#
# This is deliberately measured as a whole process, like the workload rows.
# A short row (dispatch is a few milliseconds) can otherwise read like a
# language difference when most of it is process startup and runtime setup.
set -u
cd "$(dirname "$0")/.." || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" bench/peakrss.c || { echo "peakrss build failed"; exit 2; }

to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }

cat > "$T/floor.ty" <<'EOF'
fn main():
    print("")
EOF
$TYCHOC "$T/floor.ty" --emit-c -o "$T/tycho" >/dev/null 2>&1 &&
    $CC -O3 -o "$T/tycho" "$T/tycho.c" -lm || { echo "tycho floor build failed"; exit 2; }

printf 'int main(void){return 0;}\n' > "$T/floor.c"
$CC -O3 -o "$T/c" "$T/floor.c" || { echo "C floor build failed"; exit 2; }

if command -v rustc >/dev/null 2>&1; then
    printf 'fn main() {}\n' > "$T/floor.rs"
    rustc -C opt-level=3 -o "$T/rust" "$T/floor.rs" 2>/dev/null
fi
if command -v go >/dev/null 2>&1; then
    mkdir -p "$T/go"
    printf 'package main\nfunc main() {}\n' > "$T/go/floor.go"
    ( cd "$T/go" && go build -o "$T/go/floor" floor.go 2>/dev/null )
    [ -x "$T/go/floor" ] && mv "$T/go/floor" "$T/go-floor"
fi
if command -v javac >/dev/null 2>&1; then
    mkdir -p "$T/java"
    printf 'public class floor { public static void main(String[] a) {} }\n' > "$T/java/floor.java"
    javac -d "$T/java" "$T/java/floor.java" 2>/dev/null
    printf '#!/bin/sh\nexec java -cp "%s" floor\n' "$T/java" > "$T/java-floor"
    chmod +x "$T/java-floor"
fi
if command -v swiftc >/dev/null 2>&1; then
    printf 'print("")\n' > "$T/floor.swift"
    swiftc -O -o "$T/swift" "$T/floor.swift" 2>/dev/null
fi

measure() { # <label> <binary>
    label="$1"; bin="$2"
    [ -x "$bin" ] || { printf '  %-8s %s\n' "$label" "-"; return; }
    # Two discarded warm-ups, then best time and worst peak over three runs.
    for _ in 1 2; do "$T/peakrss" "$bin" >/dev/null 2>/dev/null; done
    best=""; rss=""
    for _ in 1 2 3; do
        "$T/peakrss" "$bin" >/dev/null 2>"$T/m" || { printf '  %-8s FAILED\n' "$label"; return; }
        read kb ms < "$T/m"
        kb=$(to_kb "$kb")
        [ -z "$best" ] || [ "$ms" -lt "$best" ] && best="$ms"
        [ -z "$rss" ] || [ "$kb" -gt "$rss" ] && rss="$kb"
    done
    if [ "$best" -eq 0 ]; then
        awk "BEGIN{printf \"  %-8s %8.1f MB   <1 ms\\n\", \"$label\", $rss/1024}"
    else
        awk "BEGIN{printf \"  %-8s %8.1f MB %6d ms\\n\", \"$label\", $rss/1024, $best}"
    fi
}

echo "startup floor (empty program; two warm-ups, best of three):"
measure Tycho "$T/tycho"
measure C "$T/c"
measure Rust "$T/rust"
measure Go "$T/go-floor"
measure Java "$T/java-floor"
measure Swift "$T/swift"
