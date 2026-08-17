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
