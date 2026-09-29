#!/bin/sh
# Keep --symbols and separated CLI arguments fail-closed and in parity.
set -eu
cd "$(dirname "$0")/.."
root=$PWD
t="$(mktemp -d)"
trap 'rm -rf "$t"' EXIT HUP INT TERM
cat >"$t/good.ty" <<'TY'
fn main():
    println("ok")
TY
cat >"$t/bad.ty" <<'TY'
fn main():
    missing_function()
TY
for cc in tychoc tychoc1; do
    if ./$cc "$t/bad.ty" --symbols >"$t/$cc.sym" 2>"$t/$cc.sym.err"; then
        echo "cli-flags: $cc --symbols accepted a name-resolution error" >&2
        exit 1
    fi
    if [ ! -s "$t/$cc.sym.err" ]; then
        echo "cli-flags: $cc --symbols failed without a diagnostic" >&2
        exit 1
    fi
    if ./$cc tests/reject/recv_not_channel.ty --symbols >"$t/$cc.type.sym" 2>"$t/$cc.type.err"; then
        echo "cli-flags: $cc --symbols accepted a type error" >&2
        exit 1
    fi
    if [ -s "$t/$cc.type.sym" ] || [ ! -s "$t/$cc.type.err" ]; then
        echo "cli-flags: $cc --symbols emitted an index or no diagnostic for a type error" >&2
        exit 1
    fi
    if (cd "$t" && "$root/$cc" "$t/good.ty" -o "") >"$t/$cc.empty.out" 2>"$t/$cc.empty.err"; then
        echo "cli-flags: $cc accepted an empty -o argument" >&2
        exit 1
    fi
    if [ -e "$t/.c" ]; then
        echo "cli-flags: $cc created .c after rejecting an empty -o argument" >&2
        exit 1
    fi
    if (cd "$t" && "$root/$cc" "$t/good.ty" --emit-c -o "") >"$t/$cc.empty-c.out" 2>"$t/$cc.empty-c.err"; then
        echo "cli-flags: $cc --emit-c accepted an empty -o argument" >&2
        exit 1
    fi
    if [ -e "$t/.c" ]; then
        echo "cli-flags: $cc --emit-c created .c after rejecting an empty -o argument" >&2
        exit 1
    fi
    if ./$cc "$t/good.ty" -L -lm >"$t/$cc.link.out" 2>"$t/$cc.link.err"; then
        echo "cli-flags: $cc accepted -L without a directory" >&2
        exit 1
    fi
    if ./$cc "$t/good.ty" -I >"$t/$cc.inc.out" 2>"$t/$cc.inc.err"; then
        echo "cli-flags: $cc accepted trailing -I without a directory" >&2
        exit 1
    fi
done
echo "cli-flags: --symbols name/type errors and empty/missing output/link paths reject under both compilers"
