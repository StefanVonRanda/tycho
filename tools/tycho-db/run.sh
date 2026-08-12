#!/bin/sh
# Gate for tycho-db, the relational database in tools/tycho-db/ -- sql/ (lexer,
# parser, AST), store/ (catalogue + heap file), exec/ (the operators) and wal/
# (the write-ahead log and crash recovery).
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-db/run.sh
#
# WHAT IT ASSERTS
#   [1] the demo script's transcript matches the golden, and TWO runs from a
#       fresh store are cmp-identical -- the stdout, the store FILE and the
#       LOG file. A database betrays its caller by returning the wrong rows and
#       looking like it worked, so the golden carries the rows themselves, not
#       a count. The log is additionally asserted to be rewound to a bare
#       header after a clean run, which is what stops it growing forever.
#   [2] persistence across processes. One process writes and exits; a SECOND
#       process opens the same file and must read the rows back. The expected
#       rows are literals HERE, not a slice of the golden, so a re-record
#       cannot bless a store that lost a row. Then a THIRD process appends to
#       the reopened store and a FOURTH reads four rows -- which is the
#       fresh-store and reopened-store legs both exercised as writers.
#   [3] CRASH AND REPLAY, which is why wal/ exists. A REAL kill -9 lands
#       mid-script, between the log write and the store write; a fresh process
#       then replays and must show every completed row and no partial one.
#       Replay is asserted idempotent, and a torn trailing record is asserted
#       to be discarded rather than replayed. See the section header.
#   [4] every named variant of store.StoreErr, exec.ExecErr and wal.WalErr
#       exits non-zero with ITS OWN message. Most are reachable from the CLI
#       and are driven through it; NotAPredicate is not (see [5]).
#       ExecErr.Storage and WalErr.Storage are the wrappers the StoreErr cases
#       arrive through -- err_str delegates -- so each of those asserts it.
#       The variant list is EXTRACTED from the three enums and checked against
#       the list this runner covers, so a variant added tomorrow reddens here
#       instead of quietly going ungated.
#   [5] exec.ExecErr.NotAPredicate, which the parser cannot produce: sql
#       _predicate only ever builds Cmp or And, and _pred handles both. The
#       only caller that can reach it is one holding the AST directly, so the
#       runner copies the four packages into its temp dir, writes a probe
#       program against the exec API, and asserts the message from that.
#       Nothing is written into the repo to do it.
#
# WHAT IT DELIBERATELY DOES NOT ASSERT
#   The on-disk format's bytes, for the store or the log. Both are asserted to
#   be REPRODUCIBLE (run twice, cmp) and to round-trip through a second
#   process, which is what a caller depends on; pinning a hex dump would redden
#   for every encoding change without telling anyone whether data survived one.
#   The one exception is the log's 17-byte checkpointed header, whose SIZE is
#   checked because "the log was rewound" has no other cheap witness.
#   Timing, and the size of the store file. Neither is a promise the tool makes.
#
#   Durability against POWER LOSS. wal.ty DOES now request a flush -- core:io
#   gained io.sync (fsync) on 2026-08-12 and append/checkpoint call it -- but a
#   gate cannot cut the power, and kill -9 is a strictly weaker event that the
#   page cache alone already survives. So nothing below distinguishes a synced
#   log from an unsynced one; the kill -9 leg proves only the boundary it always
#   proved, that a record written before the process died is read by the next
#   one. What the flush buys is asserted in corelib/test/io (io.sync's statuses)
#   and argued in wal.ty's header, not measured here. A drive with a lying write
#   cache would defeat it and no test in this tree could tell.
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
# The LOG is removed alongside the store, and for a reason worth stating: a
# leftover demo.db.wal carries a base_lsn, so run 2 would number its records
# from run 1's high-water mark and stamp a different applied_lsn into the
# store. The two store files would then differ over identical input, and the
# cmp below would be reddening for the runner's own untidiness.
for n in 1 2; do
    rm -f demo.db demo.db.wal
    "$DB" demo.sql demo.db > "$T/demo.$n" 2> "$T/demo.$n.err"
    rc=$?
    [ "$rc" -eq 0 ] || { bad "demo run $n: exited $rc, expected 0"; sed 's/^/      /' "$T/demo.$n.err"; }
    [ -s "$T/demo.$n.err" ] && { bad "demo run $n: wrote to stderr"; sed 's/^/      /' "$T/demo.$n.err"; }
    if [ -f demo.db ]; then cp demo.db "$T/demo.$n.db"
    else bad "demo run $n: left no store file at all"; : > "$T/demo.$n.db"; fi
    if [ -f demo.db.wal ]; then cp demo.db.wal "$T/demo.$n.wal"
    else bad "demo run $n: left no log file at all"; : > "$T/demo.$n.wal"; fi
done
cmp -s "$T/demo.1" "$T/demo.2" || bad "the demo transcript is not deterministic"
cmp -s "$T/demo.1.db" "$T/demo.2.db" || bad "the store FILE is not reproducible from the same script"
cmp -s "$T/demo.1.wal" "$T/demo.2.wal" || bad "the LOG file is not reproducible from the same script"

# After a clean run the log must be rewound to a bare header: if a checkpoint
# left the records behind, every later open would replay work already folded
# in, and the log would grow without bound.
[ "$(wc -c < "$T/demo.1.wal")" -eq 17 ] || \
    bad "the log was not checkpointed at the end of a clean run ($(wc -c < "$T/demo.1.wal") bytes, want a 17-byte header)"

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
# [3] the crash legs: a REAL kill -9 mid-script, and the replay after it
#
# This is the point of wal/ and the only part of this runner that can prove it.
# The kill is not simulated and it is not an exit(): tycho-db's --crash-after=N
# hook runs kill(1) against its own pid, so the process dies of SIGKILL (rc
# 137) with nothing flushed -- no atexit, no buffered stdout, no store write.
# It fires after N mutations have reached the log and before the script's
# checkpoint, which is exactly the window a log exists to cover.
#
# What each leg establishes:
#   a  the crash was real: rc 137, and the process wrote NOTHING to stdout,
#      which is the evidence its buffers were never flushed.
#   b  the store file does not exist -- the mutations are in the log only.
#   c  a fresh process reads back exactly the rows whose statements COMPLETED,
#      and none of the ones that did not. Literals here, never a golden slice.
#   d  replay is idempotent: opening again, and again over a restored
#      pre-checkpoint log, must not double-apply.
#   e  a torn trailing record is DISCARDED, not replayed, and the records
#      before it still stand.
# ---------------------------------------------------------------------------
cat > crash.sql <<'EOF'
CREATE TABLE acct (id INT, who TEXT, cents INT);
INSERT INTO acct VALUES (1, 'ada', 100);
INSERT INTO acct VALUES (2, 'bob', 250);
INSERT INTO acct VALUES (3, 'cy', 375);
EOF

# CREATE is mutation 1, so --crash-after=3 dies with the table and the FIRST
# TWO rows logged, and rows 2 and 3 of the script never attempted.
rm -f k.db k.db.wal
"$DB" crash.sql k.db --crash-after=3 > "$T/k.out" 2> "$T/k.err"
krc=$?
[ "$krc" -eq 137 ] || bad "crash: exited $krc, expected 137 (SIGKILL) -- the process was not really killed"
[ -s "$T/k.out" ] && bad "crash: the killed process flushed $(wc -c < "$T/k.out") bytes of stdout -- it died too late to be a crash test"
[ -f k.db ] && bad "crash: a store file exists -- the kill did not land before the store write"
[ -s k.db.wal ] || bad "crash: the log is empty -- nothing was written ahead, so there is nothing to replay"
cp k.db.wal "$T/k.precheckpoint.wal"

# c -- recovery. Two rows were logged; the third and fourth statements never
# ran. Every row is whole: no half-written 'cy' with a missing cents.
cat > reader2.sql <<'EOF'
SELECT * FROM acct;
EOF
runs 'crash/replay (fresh process)' reader2.sql k.db
rows 'crash/replay' "$T/p.out" '1 | ada | 100' '2 | bob | 250'
# Three records, not two: the CREATE is a logged mutation as well, and it has
# to be -- a replayed INSERT with no table to insert into would fail.
grep -qxF '  wal   replayed 3 record(s)' "$T/p.out" || \
    bad "crash/replay: the recovery did not report replaying the three logged records"
printf '=== crash replay (fresh process after kill -9)\n' >> "$out"
cat "$T/p.out" >> "$out"

# d -- idempotence, twice over. First a plain reopen of the now-checkpointed
# store; then with the PRE-checkpoint log restored on top of it, which is a
# checkpoint killed after the store landed but before the log was rewound.
# Both must still show two rows.
runs 'crash/reopen' reader2.sql k.db
rows 'crash/reopen' "$T/p.out" '1 | ada | 100' '2 | bob | 250'
cp "$T/k.precheckpoint.wal" k.db.wal
runs 'crash/replay-again (interrupted checkpoint)' reader2.sql k.db
rows 'crash/replay-again' "$T/p.out" '1 | ada | 100' '2 | bob | 250'
cp "$T/k.precheckpoint.wal" k.db.wal
runs 'crash/replay-thrice' reader2.sql k.db
rows 'crash/replay-thrice' "$T/p.out" '1 | ada | 100' '2 | bob | 250'
printf '=== crash replay is idempotent (pre-checkpoint log restored)\n' >> "$out"
cat "$T/p.out" >> "$out"

# e -- a torn trailing record, in the two shapes that a length prefix alone
# cannot tell apart from a good one. Both leave the file's LENGTH untouched,
# so the frame still claims to be complete:
#
#   zeros  the last record's payload overwritten with zero bytes. This is what
#          a positional write leaves behind -- a hole reads back as zeros, not
#          as a short file.
#   flip   the last BYTE flipped. The record still parses: it is a structurally
#          valid INSERT carrying a corrupted integer. Nothing but the checksum
#          can reject this one, which is why the framing carries a checksum and
#          not just a length.
#
# Either way the record must be DISCARDED and the two before it must stand.
for shape in zeros flip; do
    rm -f z.db z.db.wal
    "$DB" crash.sql z.db --crash-after=3 > /dev/null 2>&1
    zfull=$(wc -c < z.db.wal)
    if [ "$shape" = zeros ]; then
        head -c $((zfull - 20)) z.db.wal > "$T/z.new"
        dd if=/dev/zero bs=1 count=20 >> "$T/z.new" 2>/dev/null
        cp "$T/z.new" z.db.wal
    else
        printf '\377' | dd of=z.db.wal bs=1 seek=$((zfull - 1)) conv=notrunc 2>/dev/null
    fi
    [ "$(wc -c < z.db.wal)" -eq "$zfull" ] || \
        bad "crash/torn[$shape]: the fixture changed the log's LENGTH, so it does not test the checksum"
    runs "crash/torn tail [$shape]" reader2.sql z.db
    rows "crash/torn[$shape]" "$T/p.out" '1 | ada | 100'
    grep -qxF '  wal   discarded a torn trailing record' "$T/p.out" || \
        bad "crash/torn[$shape]: the torn record was not reported as discarded"
    printf '=== crash replay with a torn trailing record [%s]\n' "$shape" >> "$out"
    cat "$T/p.out" >> "$out"
done

# ---------------------------------------------------------------------------
# [4] every named error variant
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

# NotWritten, BOTH enums' variant of it, from a single run. The store path is
# inside a directory that does not exist, so open() finds nothing (a fresh Db)
# and every write fails. A directory rather than a chmod, because chmod does
# not stop root and this must redden everywhere.
#
# The LOG is what fails first, and that ordering is the WAL contract made
# visible: the log write precedes the store write, so `cannot write
# nodir/x.db.wal` arrives at the statement (stdout) and `cannot write
# nodir/x.db` at the end-of-script checkpoint (stderr).
#
# Before wal/ this leg recorded `ok created t` followed by a failure to
# persist. That line is now GONE and its absence is asserted: the statement is
# refused at the point the log cannot take it, rather than being reported
# successful and silently lost. It is the one behaviour change in this file
# that a reader of the golden should not have to guess at.
[ -e nodir ] && bad "NotWritten: nodir exists, the fixture does not test what it claims"
printf 'CREATE TABLE t (a INT);\n' > case.sql
"$DB" case.sql nodir/x.db > "$T/c.out" 2> "$T/c.err"
rc=$?
if [ "$rc" -eq 0 ]; then
    bad "NotWritten: EXITED 0 -- an unwritable store reported success"
else
    grep -qxF 'cannot write nodir/x.db' "$T/c.err" || {
        bad "NotWritten/store: failed but not with its own message"; sed 's/^/      /' "$T/c.err"; }
    grep -qxF '  ERR   cannot write nodir/x.db.wal' "$T/c.out" || {
        bad "NotWritten/wal: the log write did not fail with its own message"; sed 's/^/      /' "$T/c.out"; }
    grep -q 'ok    created' "$T/c.out" && \
        bad "NotWritten: reported a statement SUCCEEDED that could not be logged"
fi
[ -e nodir ] && bad "NotWritten: the tool CREATED nodir/ -- it must not make a path it was not given"
[ -e nodir.wal ] && bad "NotWritten: the tool created nodir.wal"
printf '=== err NotWritten\n' >> "$out"
printf -- '--- stdout\n' >> "$out"; cat "$T/c.out" >> "$out"
printf -- '--- stderr\n' >> "$out"; cat "$T/c.err" >> "$out"

# wal.WalErr.BadLog -- the store is fine, the LOG is not. An open failure, so
# nothing runs and stdout must be empty, exactly as for a corrupt store: a
# database that cannot trust its log must not answer queries from the store
# alone, because the log is where the newest rows live.
rm -f bl.db bl.db.wal
"$DB" seed.sql bl.db > /dev/null 2>&1 || bad "BadLog: the seed script failed"
printf 'NOTALOG-not-a-tycho-db-log-at-all' > bl.db.wal
printf 'SELECT * FROM users;\n' > case.sql
"$DB" case.sql bl.db > "$T/c.out" 2> "$T/c.err"
rc=$?
if [ "$rc" -eq 0 ]; then
    bad "BadLog: EXITED 0 -- a garbage log was accepted"
elif ! grep -qxF 'not a tycho-db log: bl.db.wal' "$T/c.err"; then
    bad "BadLog: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
fi
[ -s "$T/c.out" ] && bad "BadLog: wrote to STDOUT for a database it never opened"
printf '=== err BadLog\n' >> "$out"
printf -- '--- stdout\n' >> "$out"; cat "$T/c.out" >> "$out"
printf -- '--- stderr\n' >> "$out"; cat "$T/c.err" >> "$out"

# wal.WalErr.LostPrefix -- the unrecoverable corner, and the one that must
# FAIL rather than quietly return an empty database. The store will not decode
# AND the log has been checkpointed past 0, so the records that would rebuild
# it are gone. Reported, never guessed at.
rm -f lp.db lp.db.wal
"$DB" seed.sql lp.db > /dev/null 2>&1 || bad "LostPrefix: the seed script failed"
[ "$(wc -c < lp.db.wal)" -eq 17 ] || bad "LostPrefix: the seed left an un-checkpointed log, the fixture is wrong"
printf 'TYDB1-truncated-and-unreadable' > lp.db
"$DB" case.sql lp.db > "$T/c.out" 2> "$T/c.err"
rc=$?
if [ "$rc" -eq 0 ]; then
    bad "LostPrefix: EXITED 0 -- an unrecoverable store was accepted"
elif ! grep -qxF 'lp.db is unreadable and its log no longer reaches back past lsn 2' "$T/c.err"; then
    bad "LostPrefix: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
fi
[ -s "$T/c.out" ] && bad "LostPrefix: wrote to STDOUT for a database it could not rebuild"
printf '=== err LostPrefix\n' >> "$out"
printf -- '--- stdout\n' >> "$out"; cat "$T/c.out" >> "$out"
printf -- '--- stderr\n' >> "$out"; cat "$T/c.err" >> "$out"

# ---------------------------------------------------------------------------
# [5] exec.ExecErr.NotAPredicate, which no SQL text can reach
#
# sql._predicate returns Cmp or And and nothing else, and exec._pred handles
# both -- so the arm is dead from the front end and live from the API. The
# probe holds the AST directly. The packages are COPIED into the temp dir
# because their imports are relative (`../sql`) and nothing may be written
# into the repo; the copy is also why a renamed package reddens here.
# ---------------------------------------------------------------------------
P="$T/pkg"; mkdir -p "$P"
for pkg in sql store exec wal; do
    [ -d "$src/$pkg" ] || { bad "probe: $src/$pkg is gone -- this leg asserts NOTHING"; }
    cp -R "$src/$pkg" "$P/" 2>/dev/null
done
cat > "$P/probe.ty" <<'EOF'
package main

import "sql"
import "store"
import "exec"
import "wal"

# A SELECT whose WHERE is a bare column reference -- an AST the parser cannot
# produce, so this is the only caller that can reach NotAPredicate.
#
# The log handed to exec.run is never written: SELECT is not a mutation, so
# nothing reaches it, and the probe leaves no file behind.
fn main() -> Result(void, string):
    cols: [store.Col] = []
    push(cols, store.Col("a", store.ColInt))
    row: [store.Val] = []
    push(row, store.VInt(1))
    rows: [[store.Val]] = []
    push(rows, row)
    tables: [store.Table] = []
    push(tables, store.Table("users", cols, rows))
    db := store.Db("probe.db", tables, 0)
    lg := wal.Log("probe.db.wal", 0, 0, 0)
    want: [string] = []
    match exec.run(&db, &lg, sql.Select(want, "users", Some(sql.Col("a")))):
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
# [6] the coverage floor: no variant may go ungated
#
# The three enums are READ, not remembered. A variant added to any of them
# without a leg above reddens here naming itself -- which is the only thing
# standing between "every error variant is covered" and "every error variant
# was covered on the day this was written".
#
# NotWritten is deliberately in this list once and covers both enums' variant
# of that name: store's is the failed store flush, wal's the failed log write,
# and the NotWritten leg above asserts BOTH messages from the one run.
# ---------------------------------------------------------------------------
COVERED='NoTable TableExists ColCount TypeClash Corrupt NotWritten
         Storage NoColumn Uncomparable NotAValue NotAPredicate
         Wal BadLog LostPrefix'

variants() {
    awk -v want="enum $2:" '
        $0 == want { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$1"
}

found=0
for v in $(variants "$src/store/store.ty" StoreErr) \
         $(variants "$src/exec/exec.ty" ExecErr) \
         $(variants "$src/wal/wal.ty" WalErr); do
    found=$((found + 1))
    hit=0
    for c in $COVERED; do [ "$v" = "$c" ] && hit=1; done
    [ "$hit" -eq 1 ] || bad "error variant $v has no leg in this runner -- it is UNGATED"
done
[ "$found" -ge 16 ] || bad "only $found error variant(s) found in the three enums -- the scan is broken, [4] asserts nothing"

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
    echo "tycho-db: green (demo transcript + store file + log reproducible over two runs; rows survive a process exit and a reopened store takes new writes; a real kill -9 mid-script replays to the completed rows only, idempotently, discarding a torn record; $found error variants each exit non-zero with their own message, Corrupt and BadLog with empty stdout; transcript == golden)"
else
    echo "tycho-db: FAIL"; exit 1
fi
