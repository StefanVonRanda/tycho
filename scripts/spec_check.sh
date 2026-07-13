#!/bin/sh
# spec_check.sh — the spec conformance/consistency gate (ROADMAP §1.8).
#
# Today it enforces one invariant: the collected grammar in
# docs/spec/appendix-a-grammar.md (the GENERATED region) is byte-identical to
# what scripts/gen_grammar.sh extracts from the defining chapters §3/§4. This
# makes Appendix A a checked projection of the chapters rather than a copy that
# can silently rot. Further clause->fixture checks (Appendix E) attach here.
#
# Exit 0 = spec consistent; non-zero = drift (prints a diff).
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
appendix="$root/docs/spec/appendix-a-grammar.md"
begin='<!-- BEGIN GENERATED'
end='<!-- END GENERATED -->'

fail=0

# --- Check 1: Appendix A collected grammar matches the chapters -------------
# Slice the region strictly between the marker lines (markers excluded).
committed=$(awk -v b="$begin" -v e="$end" '
    index($0,b){inr=1; next}
    index($0,e){inr=0}
    inr{print}
' "$appendix")

generated=$(sh "$root/scripts/gen_grammar.sh")

if [ "$committed" = "$generated" ]; then
    echo "spec-check: Appendix A grammar matches §3/§4 (ok)"
else
    echo "spec-check: FAIL — Appendix A grammar has drifted from §3/§4." >&2
    echo "  Regenerate with: sh scripts/gen_grammar.sh  (paste into the GENERATED region)" >&2
    echo "  --- diff (committed appendix  vs  generated) ---" >&2
    tmpc=$(mktemp); tmpg=$(mktemp)
    printf '%s\n' "$committed" > "$tmpc"
    printf '%s\n' "$generated" > "$tmpg"
    diff "$tmpc" "$tmpg" >&2 || true
    rm -f "$tmpc" "$tmpg"
    fail=1
fi

exit $fail
