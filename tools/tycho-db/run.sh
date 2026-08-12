#!/bin/sh
# Gate for tycho-db, the relational database in tools/tycho-db/ -- sql/ (lexer,
# parser, AST), store/ (catalogue + heap file) and exec/ (the operators).
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-db/run.sh
#
# WHAT IT ASSERTS
#   [1] the demo script's transcript matches the golden, and TWO runs from a
#       fresh store are cmp-identical -- both the stdout and the store FILE.
#       A database betrays its caller by returning the wrong rows and looking
#       like it worked, so the golden carries the rows themselves, not a count.
#   [2] persistence across processes. One process writes and exits; a SECOND
#       process opens the same file and must read the rows back. The expected
#       rows are literals HERE, not a slice of the golden, so a re-record
#       cannot bless a store that lost a row. Then a THIRD process appends to
#       the reopened store and a FOURTH reads four rows -- which is the
#       fresh-store and reopened-store legs both exercised as writers.
#   [3] every named variant of store.StoreErr and exec.ExecErr exits non-zero
#       with ITS OWN message. Eight are reachable from the CLI and are driven
#       through it; NotAPredicate is not (see [4]). ExecErr.Storage is the
#       wrapper the six StoreErr cases arrive through -- exec.err_str delegates
#       to store.err_str, so each of those six asserts it.
#       The variant list is EXTRACTED from the two enums and checked against
#       the list this runner covers, so a variant added tomorrow reddens here
#       instead of quietly going ungated.
#   [4] exec.ExecErr.NotAPredicate, which the parser cannot produce: sql
#       _predicate only ever builds Cmp or And, and _pred handles both. The
#       only caller that can reach it is one holding the AST directly, so the
#       runner copies the three packages into its temp dir, writes a probe
#       program against the exec API, and asserts the message from that.
#       Nothing is written into the repo to do it.
#
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   The on-disk format's bytes. The store file is asserted to be REPRODUCIBLE
#   (run twice, cmp) and to round-trip through a second process, which is what
#   a caller depends on; pinning a hex dump would redden for every encoding
#   change without telling anyone whether data survived one.
#   Timing, and the size of the file. Neither is a promise the tool makes.
#
# A NOTE ON WHAT [3]'s NotWritten LEG SHOWS. tycho-db prints `ok created t`
# and THEN fails to persist it, because run_script flushes after the last
# statement. The exit code is non-zero and the message is on stderr, so no
# caller is silently lied to, but the golden carries that stdout deliberately:
# it is the current contract, and it should redden if it changes.
#
# NO HOST DETAIL REACHES THE GOLDEN. Every store path and script path recorded
# below is RELATIVE and every recorded command runs with the temp dir as cwd,
# because tycho-db prints its store path on the first line of every run.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-db: no ./tychoc -- run 'make' first"; exit 2; }
TYCHOC="$PWD/tychoc"          # absolute: the probe in [4] is built after the cd
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-db/expected.out"
src="$PWD/tools/tycho-db"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

DB="$T/tycho-db"
if ! "$TYCHOC" "$src/main.ty" -o "$DB" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-db: FAIL"; exit 1
fi

W="$T/w"; mkdir -p "$W"
cp "$src/demo.sql" "$W/demo.sql"
cd "$W" || exit 2
out="$T/all.out"
: > "$out"

# ---------------------------------------------------------------------------
# [1] the demo, twice, from a fresh store each time
#
# Same script, same empty starting point: the two transcripts must be
# cmp-identical, and so must the two stores. A map iterated in hash order, a
# timestamp in the header or an uninitialised pad byte lands here.
# ---------------------------------------------------------------------------
for n in 1 2; do
    rm -f demo.db
    "$DB" demo.sql demo.db > "$T/demo.$n" 2> "$T/demo.$n.err"
    rc=$?
    [ "$rc" -eq 0 ] || { bad "demo run $n: exited $rc, expected 0"; sed 's/^/      /' "$T/demo.$n.err"; }
    [ -s "$T/demo.$n.err" ] && { bad "demo run $n: wrote to stderr"; sed 's/^/      /' "$T/demo.$n.err"; }
    if [ -f demo.db ]; then cp demo.db "$T/demo.$n.db"
    else bad "demo run $n: left no store file at all"; : > "$T/demo.$n.db"; fi
done
cmp -s "$T/demo.1" "$T/demo.2" || bad "the demo transcript is not deterministic"
cmp -s "$T/demo.1.db" "$T/demo.2.db" || bad "the store FILE is not reproducible from the same script"

printf '=== demo\n' >> "$out"
cat "$T/demo.1" >> "$out"

# ---------------------------------------------------------------------------
# [2] persistence: one process writes, another reads
#
# `rows <label> <file> <expected-line>...` -- the `  row` lines of a transcript
# must equal the literals given here, in order. Literals rather than a slice of
# the golden: a golden re-record blesses whatever the tool did that day, and
# "the row I inserted is still there after the process exited" is precisely the
# claim that must not be blessable.
# ---------------------------------------------------------------------------
rows() {
    _lbl=$1; _file=$2; shift 2
    grep '^  row   ' "$_file" | sed 's/^  row   //' > "$T/got"
    : > "$T/want"
    for _r in "$@"; do printf '%s\n' "$_r" >> "$T/want"; done
    cmp -s "$T/got" "$T/want" || {
        bad "$_lbl: rows differ from the expected literals"
        diff "$T/want" "$T/got" | sed 's/^/      /'
    }
}

runs() {
    _lbl=$1; _script=$2; _store=$3
    "$DB" "$_script" "$_store" > "$T/p.out" 2> "$T/p.err"
    _rc=$?
    [ "$_rc" -eq 0 ] || { bad "$_lbl: exited $_rc, expected 0"; sed 's/^/      /' "$T/p.err"; }
    [ -s "$T/p.err" ] && { bad "$_lbl: wrote to stderr"; sed 's/^/      /' "$T/p.err"; }
}

cat > writer.sql <<'EOF'
CREATE TABLE people (id INT, name TEXT, age INT);
INSERT INTO people VALUES (1, 'ada', 36);
INSERT INTO people VALUES (2, 'bob', 41);
INSERT INTO people VALUES (3, 'cy', 29);
EOF
cat > reader.sql <<'EOF'
SELECT * FROM people;
EOF
cat > appender.sql <<'EOF'
INSERT INTO people VALUES (4, 'dee', 55);
EOF
cat > filter.sql <<'EOF'
SELECT name, age FROM people WHERE age > 30 AND name != 'bob';
EOF

rm -f p.db
# process 1 -- a FRESH store, writes only, prints no rows at all
runs 'persist/write (fresh store)' writer.sql p.db
rows 'persist/write' "$T/p.out"
[ -f p.db ] || bad "persist: the writer process left no store file"

# process 2 -- reads what process 1 left behind
runs 'persist/read (reopened store)' reader.sql p.db
rows 'persist/read' "$T/p.out" '1 | ada | 36' '2 | bob | 41' '3 | cy | 29'
cp "$T/p.out" "$T/read.1"

# process 3 -- WRITES to the reopened store, which is the leg a store that
# only ever round-trips a file it created would pass without doing
runs 'persist/append (reopened store)' appender.sql p.db
# process 4 -- all four rows, including the one written after the reopen
runs 'persist/reread' reader.sql p.db
rows 'persist/reread' "$T/p.out" '1 | ada | 36' '2 | bob | 41' '3 | cy | 29' '4 | dee | 55'

printf '=== persist read-back (second process)\n' >> "$out"
cat "$T/read.1" >> "$out"
printf '=== persist reread after append (fourth process)\n' >> "$out"
cat "$T/p.out" >> "$out"

# the predicate, over the reopened store: a decoded row must compare the same
# way a freshly inserted one does
runs 'persist/filter' filter.sql p.db
rows 'persist/filter' "$T/p.out" 'ada | 36' 'dee | 55'
printf '=== persist filter\n' >> "$out"
cat "$T/p.out" >> "$out"

# ---------------------------------------------------------------------------
# [3] every named error variant
#
# stmt_err <variant> <expected message> <sql> -- a statement that fails inside
# a script: non-zero exit, `  ERR   <message>` on stdout, and stderr naming the
# failed count. seeded against a table this function creates itself.
#
# The message is compared WHOLE, not by substring: `no such table: ghosts`
# passing on a tool that printed `no such table:` would be a gate agreeing with
# a diagnostic that names nothing.
# ---------------------------------------------------------------------------
cat > seed.sql <<'EOF'
CREATE TABLE users (id INT, name TEXT, age INT);
INSERT INTO users VALUES (1, 'ada', 36);
EOF

stmt_err() {
    _var=$1; _want=$2; _sql=$3
    rm -f e.db
    "$DB" seed.sql e.db > "$T/seed.out" 2> "$T/seed.err" || {
        bad "$_var: the seed script failed"; sed 's/^/      /' "$T/seed.err"; return
    }
    printf '%s\n' "$_sql" > case.sql
    "$DB" case.sql e.db > "$T/c.out" 2> "$T/c.err"
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        bad "$_var: EXITED 0 -- the error did not fire"
    elif ! grep -qxF "  ERR   $_want" "$T/c.out"; then
        bad "$_var: failed but not with its own message (want: $_want)"
        sed 's/^/      /' "$T/c.out" "$T/c.err"
    elif ! grep -qF 'statement(s) failed' "$T/c.err"; then
        bad "$_var: the process did not report the failed statement on stderr"
        sed 's/^/      /' "$T/c.err"
    fi
    printf '=== err %s\n' "$_var" >> "$out"
    printf -- '--- stdout\n' >> "$out"; cat "$T/c.out" >> "$out"
    printf -- '--- stderr\n' >> "$out"; cat "$T/c.err" >> "$out"
}

# store.StoreErr, reached through exec.ExecErr.Storage, which is what
# exec.err_str delegates to store.err_str for
stmt_err NoTable      'no such table: ghosts'              'SELECT * FROM ghosts;'
stmt_err TableExists  'table already exists: users'        'CREATE TABLE users (id INT);'
stmt_err ColCount     'users takes 3 value(s), got 2'      "INSERT INTO users VALUES (1, 'x');"
stmt_err TypeClash    'users.id is int, got text'          "INSERT INTO users VALUES ('x', 'y', 1);"
# exec.ExecErr
stmt_err NoColumn     'users has no column nope'           'SELECT nope FROM users;'
stmt_err Uncomparable 'cannot compare int with text'       "SELECT * FROM users WHERE age > 'x';"
stmt_err NotAValue    'not a value: id'                    "INSERT INTO users VALUES (id, 'y', 1);"

# store.StoreErr.Corrupt -- an OPEN failure, so nothing runs and stdout must be
# EMPTY. A database that prints a header for a file it could not read hands the
# caller a transcript it cannot tell is empty of results.
printf 'NOTADB-not-a-tycho-db-store' > bad.db
printf 'SELECT * FROM users;\n' > case.sql
"$DB" case.sql bad.db > "$T/c.out" 2> "$T/c.err"
rc=$?
if [ "$rc" -eq 0 ]; then
    bad "Corrupt: EXITED 0 -- a garbage file was accepted as a store"
elif ! grep -qxF 'store is corrupt: bad.db is not a tycho-db store' "$T/c.err"; then
    bad "Corrupt: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
fi
[ -s "$T/c.out" ] && bad "Corrupt: wrote $(wc -c < "$T/c.out") bytes to STDOUT for a store it never opened"
printf '=== err Corrupt\n' >> "$out"
printf -- '--- stdout\n' >> "$out"; cat "$T/c.out" >> "$out"
printf -- '--- stderr\n' >> "$out"; cat "$T/c.err" >> "$out"

# store.StoreErr.NotWritten -- a FLUSH failure. The store path is inside a
# directory that does not exist, so open() finds nothing (a fresh Db) and the
# write at the end of the script is what fails. A directory rather than a
# chmod, because chmod does not stop root and this must redden everywhere.
[ -e nodir ] && bad "NotWritten: nodir exists, the fixture does not test what it claims"
printf 'CREATE TABLE t (a INT);\n' > case.sql
"$DB" case.sql nodir/x.db > "$T/c.out" 2> "$T/c.err"
rc=$?
if [ "$rc" -eq 0 ]; then
    bad "NotWritten: EXITED 0 -- an unwritable store reported success"
elif ! grep -qxF 'cannot write nodir/x.db' "$T/c.err"; then
    bad "NotWritten: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
fi
[ -e nodir ] && bad "NotWritten: the tool CREATED nodir/ -- it must not make a path it was not given"
printf '=== err NotWritten\n' >> "$out"
printf -- '--- stdout\n' >> "$out"; cat "$T/c.out" >> "$out"
printf -- '--- stderr\n' >> "$out"; cat "$T/c.err" >> "$out"

# ---------------------------------------------------------------------------
# [4] exec.ExecErr.NotAPredicate, which no SQL text can reach
#
# sql._predicate returns Cmp or And and nothing else, and exec._pred handles
# both -- so the arm is dead from the front end and live from the API. The
# probe holds the AST directly. The packages are COPIED into the temp dir
# because their imports are relative (`../sql`) and nothing may be written
# into the repo; the copy is also why a renamed package reddens here.
# ---------------------------------------------------------------------------
P="$T/pkg"; mkdir -p "$P"
for pkg in sql store exec; do
    [ -d "$src/$pkg" ] || { bad "probe: $src/$pkg is gone -- this leg asserts NOTHING"; }
    cp -R "$src/$pkg" "$P/" 2>/dev/null
done
cat > "$P/probe.ty" <<'EOF'
package main

import "sql"
import "store"
import "exec"

# A SELECT whose WHERE is a bare column reference -- an AST the parser cannot
# produce, so this is the only caller that can reach NotAPredicate.
fn main() -> Result(void, string):
    cols: [store.Col] = []
    push(cols, store.Col("a", store.ColInt))
    row: [store.Val] = []
    push(row, store.VInt(1))
    rows: [[store.Val]] = []
    push(rows, row)
    tables: [store.Table] = []
    push(tables, store.Table("users", cols, rows))
    db := store.Db("probe.db", tables)
    want: [string] = []
    match exec.run(&db, sql.Select(want, "users", Some(sql.Col("a")))):
        Ok(o): return Err("exec.run ACCEPTED a non-predicate WHERE")
        Err(e): return Err(exec.err_str(e))
EOF
if ! "$TYCHOC" "$P/probe.ty" -o "$T/probe" >"$T/probe.log" 2>&1; then
    bad "probe: tychoc could not build the NotAPredicate probe"
    sed 's/^/      /' "$T/probe.log" | head -8
else
    "$T/probe" > "$T/c.out" 2> "$T/c.err"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        bad "NotAPredicate: EXITED 0 -- exec.run accepted a bare column as a predicate"
    elif ! grep -qxF 'not a predicate: a' "$T/c.err"; then
        bad "NotAPredicate: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
    fi
    [ -s "$T/c.out" ] && bad "NotAPredicate: wrote to STDOUT"
    printf '=== err NotAPredicate (via the exec API)\n' >> "$out"
    cat "$T/c.err" >> "$out"
fi

# ---------------------------------------------------------------------------
# [5] the coverage floor: no variant may go ungated
#
# The two enums are READ, not remembered. A variant added to either without a
# leg above reddens here naming itself -- which is the only thing standing
# between "every error variant is covered" and "every error variant was
# covered on the day this was written".
# ---------------------------------------------------------------------------
COVERED='NoTable TableExists ColCount TypeClash Corrupt NotWritten
         Storage NoColumn Uncomparable NotAValue NotAPredicate'

variants() {
    awk -v want="enum $2:" '
        $0 == want { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$1"
}

found=0
for v in $(variants "$src/store/store.ty" StoreErr) $(variants "$src/exec/exec.ty" ExecErr); do
    found=$((found + 1))
    hit=0
    for c in $COVERED; do [ "$v" = "$c" ] && hit=1; done
    [ "$hit" -eq 1 ] || bad "error variant $v has no leg in this runner -- it is UNGATED"
done
[ "$found" -ge 11 ] || bad "only $found error variant(s) found in the two enums -- the scan is broken, [3] asserts nothing"

# ---------------------------------------------------------------------------
# the golden
# ---------------------------------------------------------------------------
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-db"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-db/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-db: green (demo transcript + store file reproducible over two runs; rows survive a process exit and a reopened store takes new writes; $found error variants each exit non-zero with their own message, Corrupt with empty stdout; transcript == golden)"
else
    echo "tycho-db: FAIL"; exit 1
fi
