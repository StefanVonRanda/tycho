#!/bin/sh
# Stage E dogfood: split the single-file self-hosted compiler tychoc0.ty into a
# two-package program and write it to <outdir>:
#
#   <outdir>/main.ty      package main  — the lexer/parser/typecheck/codegen,
#                                          importing "rt"; the 5 emitter calls
#                                          are qualified rt.<name>(...)
#   <outdir>/rt/rt.ty     package rt    — the pure C-runtime / string emitters
#                                          (preamble, gen_strlib, gen_mhash,
#                                          gen_map_type, gen_map_fns): leaf
#                                          functions with primitive signatures,
#                                          no compiler types, no calls back into
#                                          main, so the cut is narrow (a one-way
#                                          main -> rt dependency).
#
# Generated from tychoc0.ty by function NAME (robust to line shifts), so it never
# drifts: the split is always exactly the current compiler, just repackaged.
#
# ORPHANED 2026-07-29. Its only caller was compiler/fixpoint.sh, which
# regenerated the split and asserted it (a) self-hosted byte-identically and (b)
# emitted identical C to the single-file compiler on every fixture. That lane is
# RETIRED — see the header of compiler/fixpoint.sh for why, and ROADMAP.md /
# docs/architecture.md for what the project gave up. This script builds no
# compiler itself; it only rewrites compiler/tychoc0.ty, which stays on disk. It
# is kept, not deleted, because it is the only record of where the compiler's
# one clean package seam actually falls (a one-way main -> rt dependency), and
# that is worth having if the multi-package path is ever revisited.
set -eu
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
H="$HERE/tychoc0.ty"
OUT="$1"
# gen_map_type/gen_map_fns moved to MAIN: they are now type-aware (use cty/
# cp_field/mangle, all in main) like gen_arr_fns, so they can't sit in the lower
# rt layer. main() calls them, so the cut stays a clean one-way main -> rt.
RT_FNS="preamble gen_strlib gen_mhash"

mkdir -p "$OUT/rt"

# Tag every line MAIN/RT by the top-level definition it falls under (a function
# whose name is in RT_FNS routes to RT; everything else, incl. all types and the
# file header, to MAIN).
awk -v rt="$RT_FNS" '
BEGIN { n = split(rt, a, " "); for (i = 1; i <= n; i++) isrt[a[i]] = 1; cur = "MAIN" }
/^(fn |struct |enum )/ {
    cur = "MAIN"
    if ($1 == "fn") { name = $2; sub(/\(.*/, "", name); if (name in isrt) cur = "RT" }
}
{ print cur "\t" $0 }
' "$H" > "$OUT/.tagged"

{ echo "package rt"; echo; sed -n 's/^RT\t//p' "$OUT/.tagged"; } > "$OUT/rt/rt.ty"
{
    echo "package main"
    echo 'import "rt"'
    echo
    # Qualify the cross-package emitter calls (their definitions now live in rt).
    # sed -E (not GNU \| alternation) so BSD/macOS sed works too; the leading
    # (^|[^A-Za-z0-9_]) guard stops substring hits like `xpreamble(`.
    sed -n 's/^MAIN\t//p' "$OUT/.tagged" \
        | sed -E 's/(^|[^A-Za-z0-9_])(preamble|gen_strlib|gen_mhash)\(/\1rt.\2(/g'
} > "$OUT/main.ty"

rm -f "$OUT/.tagged"
