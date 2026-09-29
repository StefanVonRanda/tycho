#!/bin/sh
# D11 regression: source input may be a pipe, FIFO or /dev/stdin.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

for compiler in ./tychoc ./tychoc1; do
    printf 'fn main():\n    println("pipe-ok")\n' |
        "$compiler" /dev/stdin --emit-c -o "$tmp/pipe"
    grep -q 'pipe-ok' "$tmp/pipe.c"

    mkfifo "$tmp/source.fifo"
    (printf 'fn main():\n    println("fifo-ok")\n' > "$tmp/source.fifo") &
    writer=$!
    "$compiler" "$tmp/source.fifo" --emit-c -o "$tmp/fifo"
    wait "$writer"
    grep -q 'fifo-ok' "$tmp/fifo.c"
    rm -f "$tmp/source.fifo"
done
echo "stream-source: /dev/stdin and FIFO inputs passed for both compilers"
