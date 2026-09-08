#!/bin/sh
# Spelling gate for src_in_corelib (src/tychoc.c@src_in_corelib).
#
# src_in_corelib() suppresses the unused-local check for files under the
# corelib root.  The gate proves the check is a FILESYSTEM test (via
# under_corelib), not a spelling test (strstr "corelib/").  A spelling
# regression would let a file whose PATH contains "corelib/" but whose
# LOCATION is outside the corelib root skip the check.
#
# The test: compile a file OUTSIDE corelib that declares an unused local.
# If the unused-local error fires, the filesystem check is working.
# If it does not, the check has regressed to something that exempts this file.
set -u
cd "$(dirname "$0")/.." || exit 2

TYCHOC="./tychoc"
[ -x "$TYCHOC" ] || { echo "SKIP  spelling_gate_corelib  (tychoc not built)"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# A file outside corelib with an unused local.  The path deliberately
# does NOT contain "corelib/", so a spelling test is vacuous here;
# the filesystem check must confirm the file is outside the corelib root.
cat > "$TMP/unused_local.ty" << 'EOF'
fn main():
    x := 42
    println("hello")
EOF

err=$("$TYCHOC" "$TMP/unused_local.ty" 2>&1)
rc=$?

if [ "$rc" -ne 0 ] && echo "$err" | grep -q "declared and not used"; then
    echo "PASS  spelling_gate_corelib"
    exit 0
else
    echo "FAIL  spelling_gate_corelib: expected unused-local error, got rc=$rc: $err"
    exit 1
fi
