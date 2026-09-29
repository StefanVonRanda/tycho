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

    # procfs reports st_size=0 despite serving bytes. The native loader must
    # read to EOF rather than treating this as an empty source file.
    if [ -r /proc/self/cmdline ]; then
        if "$compiler" /proc/self/cmdline --emit-c -o "$tmp/proc-${compiler#./}" \
            >"$tmp/proc.out" 2>"$tmp/proc.err"; then
            echo "$compiler: accepted /proc/self/cmdline as empty source" >&2
            exit 1
        fi
    fi
done
echo "stream-source: /dev/stdin, FIFO and zero-st_size procfs inputs passed for both compilers"
