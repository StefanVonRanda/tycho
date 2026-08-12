#!/bin/sh
# Gate for tycho-db, the relational database in tools/tycho-db/ -- sql/ (lexer,
# parser, AST), store/ (catalogue, heap file and equality index), plan/ (access
# path selection and constant folding), exec/ (the operators), wal/ (the
# write-ahead log and crash recovery) and srv/ (the line protocol and client).
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
#   [3b] THE PLANNER EARNS ITS PACKAGE. Not a golden diff: the index path and
#       the scan path are run over identical rows and asserted to return THE
#       SAME rows, then asserted to differ in what they EXAMINED (1 against 6)
#       -- correctness and "the index actually ran" are separate claims and a
#       silent fallback to a scan would pass the first alone. Constant folding
#       is asserted by a `WHERE 1 = 2` examining zero rows of a table holding
#       six. See the section header.
#   [4] every named variant of store.StoreErr, exec.ExecErr, wal.WalErr,
#       plan.PlanErr and srv.SrvErr exits non-zero with ITS OWN message. Most
#       are reachable from the CLI and are driven through it; NotAPredicate and NoIndex are
#       not (see [5]); srv.Accept needs a broken listening socket, which a
#       gate that has one has a broken server rather than a test.
#       ExecErr.Storage and WalErr.Storage are the wrappers the StoreErr cases
#       arrive through -- err_str delegates -- so each of those asserts it.
#       The variant list is EXTRACTED from the five enums and checked against
#       the list this runner covers, so a variant added tomorrow reddens here
#       instead of quietly going ungated.
#   [5] the two variants no SQL text can reach, each probed through the API
#       that owns it. exec.ExecErr.NotAPredicate: sql._predicate only ever
#       builds Cmp or And and exec._pred handles both, so only a caller holding
#       the AST can get there. store.StoreErr.NoIndex: plan asks which columns
#       are indexed before it commits to a probe, so only a caller that skips
#       that question can get there -- and asserting the REFUSAL is what rules
#       out a probe() that quietly falls back to a scan. The runner copies the
#       packages into its temp dir and builds a probe against each API;
#       nothing is written into the repo to do it.
#   [5b] THE SERVER, over real sockets and nothing mocked: a session through
#       tycho-db's own client, a SECOND session that must see the first one's
#       writes, a RAW SOCKET client asserting the wire format byte for byte,
#       two rude clients that must end their own session and not the daemon,
#       and two concurrent clients that must both be answered correctly.
#       The port is discovered from the banner -- no fixed port, no sleep.
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
# [3b] THE PLANNER, and the only assertions that can tell it from theatre
#
# plan/ chooses between two access paths -- store.scan and store.probe, the
# equality index behind a KEY column -- folds a constant WHERE, and carries the
# projection. A layer that merely renamed the AST would pass a golden diff
# happily, so none of the legs below is a golden diff:
#
#   a  THE DIFFERENTIAL. Two tables, byte-identical rows, one with KEY on id
#      and one without. The same query against both must return THE SAME ROWS
#      -- an index that returns different rows from the scan is the worst bug
#      this tool could have, and it would be invisible to a transcript that
#      only ever asks one of them. Every key present in the table is probed,
#      plus one that is absent.
#   b  AND THE PATHS MUST ACTUALLY DIFFER. Identical rows from both proves
#      correctness, not that the index ran at all -- a probe silently falling
#      back to a scan would pass (a). So the EXAMINED counter is asserted too:
#      1 through the index against 6 through the scan, over the same table and
#      the same answer.
#   c  CONSTANT FOLDING, in the shape whose effect nothing else could produce:
#      `WHERE 1 = 2` over a table with six rows must examine ZERO. A layer that
#      passed the predicate through would examine six and return none.
#   d  EXPLAIN names the path it chose, and folding is visible in the residual.
#   e  the counters are not decoration: rows returned <= rows examined, always.
# ---------------------------------------------------------------------------
cat > plan_seed.sql <<'EOF'
CREATE TABLE idx (id INT KEY, name TEXT);
CREATE TABLE noidx (id INT, name TEXT);
INSERT INTO idx VALUES (10, 'ada');
INSERT INTO idx VALUES (20, 'bob');
INSERT INTO idx VALUES (30, 'cy');
INSERT INTO idx VALUES (40, 'dee');
INSERT INTO idx VALUES (50, 'eve');
INSERT INTO idx VALUES (60, 'fay');
INSERT INTO noidx VALUES (10, 'ada');
INSERT INTO noidx VALUES (20, 'bob');
INSERT INTO noidx VALUES (30, 'cy');
INSERT INTO noidx VALUES (40, 'dee');
INSERT INTO noidx VALUES (50, 'eve');
INSERT INTO noidx VALUES (60, 'fay');
EOF
rm -f pl.db pl.db.wal
runs 'plan/seed' plan_seed.sql pl.db

# a -- the differential, over every key in the table and one that is not
for k in 10 20 30 40 50 60 99; do
    printf 'SELECT name FROM idx WHERE id = %s;\n' "$k" > case.sql
    runs "plan/index k=$k" case.sql pl.db
    grep '^  row   ' "$T/p.out" > "$T/idx.rows"
    printf 'SELECT name FROM noidx WHERE id = %s;\n' "$k" > case.sql
    runs "plan/scan k=$k" case.sql pl.db
    grep '^  row   ' "$T/p.out" > "$T/scan.rows"
    cmp -s "$T/idx.rows" "$T/scan.rows" || {
        bad "plan/differential k=$k: the index path and the scan path returned DIFFERENT rows"
        diff "$T/scan.rows" "$T/idx.rows" | sed 's/^/      /'
    }
done

# b -- and the two paths must not be the same path wearing two names
examined() {
    sed -n 's/^  ok    [0-9]* row(s) from \([0-9]*\) examined$/\1/p' "$1" | tr '\n' ' '
}
printf 'SELECT name FROM idx WHERE id = 30;\n' > case.sql
runs 'plan/examined index' case.sql pl.db
ix_ex=$(examined "$T/p.out"); ix_ex=${ix_ex% }
rows 'plan/examined index' "$T/p.out" 'cy'
printf 'SELECT name FROM noidx WHERE id = 30;\n' > case.sql
runs 'plan/examined scan' case.sql pl.db
sc_ex=$(examined "$T/p.out"); sc_ex=${sc_ex% }
rows 'plan/examined scan' "$T/p.out" 'cy'
[ "$ix_ex" = 1 ] || bad "plan/index: examined $ix_ex rows, want 1 -- the probe did not narrow anything, so the index path is not being taken"
[ "$sc_ex" = 6 ] || bad "plan/scan: examined $sc_ex rows, want 6 -- the scan is not reading the whole table"

# c -- constant folding: a false WHERE must not open the table at all
printf 'SELECT name FROM idx WHERE 1 = 2;\n' > case.sql
runs 'plan/fold false' case.sql pl.db
rows 'plan/fold false' "$T/p.out"
fold_ex=$(examined "$T/p.out"); fold_ex=${fold_ex% }
[ "$fold_ex" = 0 ] || bad "plan/fold: a constant-false WHERE examined $fold_ex rows, want 0 -- the predicate was not folded, it was evaluated"

# a constant-TRUE conjunct must vanish from the residual and change no answer
printf 'SELECT name FROM idx WHERE 1 = 1 AND id = 20;\n' > case.sql
runs 'plan/fold true' case.sql pl.db
rows 'plan/fold true' "$T/p.out" 'bob'

# d -- EXPLAIN names the chosen path, and shows what folding left behind
cat > explain.sql <<'EOF'
EXPLAIN SELECT name FROM idx WHERE id = 20;
EXPLAIN SELECT name FROM noidx WHERE id = 20;
EXPLAIN SELECT name FROM idx WHERE 1 = 2;
EXPLAIN SELECT name FROM idx WHERE 1 = 1 AND id = 20;
EXPLAIN SELECT name FROM idx WHERE id = 20 AND name != 'zz';
EOF
runs 'plan/explain' explain.sql pl.db
for want in \
    '  plan  access: index idx.id = 20' \
    '  plan  access: scan noidx' \
    '  plan  access: none (WHERE is constant false)' \
    '  plan  residual: name != '"'"'zz'"'"''
do
    grep -qxF "$want" "$T/p.out" || bad "plan/explain: no line [$want]"
done
# The folded conjunct must be GONE from the residual, not merely reported.
# Scoped to the residual lines: the transcript echoes each script line, so the
# `1 = 1` in the SOURCE is present either way and searching the whole file
# would redden for a planner that folded perfectly.
grep '^  plan  residual:' "$T/p.out" | grep -qF '1 = 1' && \
    bad "plan/explain: a constant-true conjunct survived into the residual -- it was not folded"
printf '=== plan EXPLAIN\n' >> "$out"
cat "$T/p.out" >> "$out"

# e -- returned never exceeds examined, over every SELECT this runner has run
# `  ok    2 row(s) from 4 examined` -- awk strips the indent, so returned is
# $2 and examined is $5.
counted=$(grep -c 'row(s) from' "$out")
[ "$counted" -ge 8 ] || bad "plan/counters: only $counted counted SELECT(s) in the transcript -- this leg is asserting almost nothing"
awk '/^  ok    [0-9]+ row\(s\) from [0-9]+ examined$/ {
        if ($2 + 0 > $5 + 0) { print "      " $0; over = 1 }
     } END { exit over + 0 }' "$out" || \
    bad "plan/counters: a SELECT returned more rows than its access path examined"

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
# plan.PlanErr, through exec.ExecErr.Plan -- EXPLAIN of a statement that has no
# access path to choose. Reachable from SQL text, unlike NoIndex below.
stmt_err NotSelect    "cannot plan (only SELECT has a plan): INSERT users (2, 'x', 1)" \
                                                           "EXPLAIN INSERT INTO users VALUES (2, 'x', 1);"

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
for pkg in sql store exec wal plan; do
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
    push(cols, store.Col("a", store.ColInt, false))
    row: [store.Val] = []
    push(row, store.VInt(1))
    rows: [[store.Val]] = []
    push(rows, row)
    idx: [store.Index] = []
    tables: [store.Table] = []
    push(tables, store.Table("users", cols, rows, idx))
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

# store.StoreErr.NoIndex, which no SQL text can reach either, and for a reason
# worth keeping true: plan asks store which columns are indexed BEFORE it
# commits to a probe, so the planner never requests the index path on a column
# that has none. The variant guards the store API against a caller that skips
# that question -- and this probe is that caller.
#
# It is also the assertion that probe() does not quietly fall back to a scan.
# A fallback would return the right rows here and exit 0, which is exactly the
# failure the variant exists to prevent, so the probe asserts the REFUSAL.
#
# In its OWN directory: tychoc compiles every .ty beside the entry file, so two
# probe programs in one directory are two `main`s in one package.
P2="$T/pkg2"; mkdir -p "$P2"
cp -R "$src/store" "$P2/" 2>/dev/null
cat > "$P2/probe2.ty" <<'EOF'
package main

import "store"

fn main() -> Result(void, string):
    cols: [store.Col] = []
    push(cols, store.Col("id", store.ColInt, false))     # NOT indexed
    row: [store.Val] = []
    push(row, store.VInt(1))
    rows: [[store.Val]] = []
    push(rows, row)
    idx: [store.Index] = []
    tables: [store.Table] = []
    push(tables, store.Table("users", cols, rows, idx))
    db := store.Db("probe.db", tables, 0)
    match store.probe(db, "users", "id", store.VInt(1)):
        Ok(rs): return Err("store.probe SERVED an index path on a column with no index")
        Err(e): return Err(store.err_str(e))
EOF
if ! "$TYCHOC" "$P2/probe2.ty" -o "$T/probe2" >"$T/probe2.log" 2>&1; then
    bad "probe: tychoc could not build the NoIndex probe"
    sed 's/^/      /' "$T/probe2.log" | head -8
else
    "$T/probe2" > "$T/c.out" 2> "$T/c.err"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        bad "NoIndex: EXITED 0 -- store.probe accepted a column with no index"
    elif ! grep -qxF 'users.id has no index' "$T/c.err"; then
        bad "NoIndex: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
    fi
    [ -s "$T/c.out" ] && bad "NoIndex: wrote to STDOUT"
    printf '=== err NoIndex (via the store API)\n' >> "$out"
    cat "$T/c.err" >> "$out"
fi

# ---------------------------------------------------------------------------
# [5b] THE SERVER, over real sockets
#
# Every leg here opens a TCP connection to a tycho-db that is actually running.
# Nothing is mocked and no srv function is called in-process except the one
# variant a socket cannot reach (Accept, at the end).
#
# NO FIXED PORT: the server binds port 0 and prints the port the kernel gave it
# on stderr, which is also the readiness signal -- the banner is printed after
# the socket is listening and before the first accept, so a client that
# connects on seeing it is queued rather than refused. No sleep is used to wait
# for readiness anywhere below.
#
#   a  a session through tycho-db's own client: the rows are literals here.
#   b  a SECOND session, new connection, must see the first one's writes -- the
#      one claim a single-session test cannot make, and the reason the server
#      checkpoints at the end of every session rather than at exit.
#   c  a RAW SOCKET client speaks the protocol. This is what stops the wire
#      format from being whatever our own client happens to send: it asserts
#      the response block byte for byte, terminator included.
#   d  the daemon SURVIVES a rude client. A peer that hangs up mid-statement,
#      and a peer that overruns the line cap, must each end their own session
#      and nothing more -- proven by a THIRD client being answered afterwards.
#   e  sessions are SERIALISED, which is this layer's stated concurrency
#      decision: two clients launched at once must both complete correctly,
#      the second having waited in the backlog.
# ---------------------------------------------------------------------------
SRVPID=""
srv_stop() {
    [ -n "$SRVPID" ] && kill -TERM "$SRVPID" 2>/dev/null
    [ -n "$SRVPID" ] && wait "$SRVPID" 2>/dev/null
    SRVPID=""
}
trap 'srv_stop; rm -rf "$T"' EXIT INT TERM

# Start a server on port 0 and set $port from the banner. No sleep-then-hope:
# the banner cannot appear before the socket is listening.
srv_start() {
    _store=$1
    : > "$T/srv.log"
    "$DB" --serve --port=0 "$_store" >"$T/srv.out" 2>"$T/srv.log" &
    SRVPID=$!
    port=""
    i=0
    while [ -z "$port" ] && [ "$i" -lt 200 ]; do
        port=$(sed -n 's/^tycho-db: listening on [^:]*:\([0-9][0-9]*\) .*$/\1/p' "$T/srv.log" | head -1)
        [ -z "$port" ] && sleep 0.05
        i=$((i + 1))
    done
    [ -n "$port" ]
}

rm -f n.db n.db.wal
if ! srv_start n.db; then
    bad "server: no banner on stderr within 10s"; sed 's/^/      /' "$T/srv.log"
else
    [ "$port" -gt 0 ] 2>/dev/null || bad "server: banner did not name a bound port (got '$port')"

    # a -- a session through our own client
    cat > s1.sql <<'EOF'
CREATE TABLE people (id INT KEY, name TEXT, age INT);
INSERT INTO people VALUES (1, 'ada', 36);
INSERT INTO people VALUES (2, 'bob', 41);
INSERT INTO people VALUES (3, 'cy', 29);
SELECT * FROM people;
EXPLAIN SELECT name FROM people WHERE id = 2;
SELECT name FROM people WHERE id = 2;
SELECT name FROM people WHERE age = 41;
EOF
    "$DB" "--client=127.0.0.1:$port" s1.sql > "$T/s1.out" 2> "$T/s1.err"
    rc=$?
    [ "$rc" -eq 0 ] || { bad "server/session1: client exited $rc"; sed 's/^/      /' "$T/s1.err"; }
    # `row ` not `  row   `: this is the WIRE shape, indented two by the client
    srows() {
        _lbl=$1; _file=$2; shift 2
        grep '^  row ' "$_file" | sed 's/^  row //' > "$T/got"
        : > "$T/want"
        for _r in "$@"; do printf '%s\n' "$_r" >> "$T/want"; done
        cmp -s "$T/got" "$T/want" || {
            bad "$_lbl: rows differ from the expected literals"
            diff "$T/want" "$T/got" | sed 's/^/      /'
        }
    }
    srows 'server/session1' "$T/s1.out" '1|ada|36' '2|bob|41' '3|cy|29' 'bob' 'bob'
    grep -qxF '  plan access: index people.id = 2' "$T/s1.out" || \
        bad "server/session1: the planner's index choice did not survive the wire"
    grep -qxF '  ok 1 row(s) from 1 examined' "$T/s1.out" || \
        bad "server/session1: the examined counter did not survive the wire"
    grep -qxF '  ok 1 row(s) from 3 examined' "$T/s1.out" || \
        bad "server/session1: the scan path's counter did not survive the wire"
    printf '=== server session 1 (own client, over TCP)\n' >> "$out"
    cat "$T/s1.out" >> "$out"

    # b -- a NEW connection must see what the first one wrote
    printf 'SELECT * FROM people;\n' > s2.sql
    "$DB" "--client=127.0.0.1:$port" s2.sql > "$T/s2.out" 2> "$T/s2.err"
    rc=$?
    [ "$rc" -eq 0 ] || { bad "server/session2: client exited $rc"; sed 's/^/      /' "$T/s2.err"; }
    srows 'server/session2' "$T/s2.out" '1|ada|36' '2|bob|41' '3|cy|29'
    [ -f n.db ] || bad "server: no store file after a session -- the checkpoint never ran"
    [ "$(wc -c < n.db.wal)" -eq 17 ] || \
        bad "server: the log was not rewound after the session ($(wc -c < n.db.wal) bytes, want 17)"
    printf '=== server session 2 (new connection sees session 1 writes)\n' >> "$out"
    cat "$T/s2.out" >> "$out"

    # c -- the RAW protocol, byte for byte
    python3 - "$port" > "$T/raw.out" 2> "$T/raw.err" <<'PYEOF'
import socket, sys
sys.stdout.reconfigure(newline="\n")
port = int(sys.argv[1])
s = socket.create_connection(("127.0.0.1", port)); s.settimeout(5)
buf = b""
def block(stmt):
    """Send one statement, read lines until the lone '.' terminator."""
    global buf
    s.sendall(stmt.encode() + b"\n")
    out = []
    while True:
        while b"\n" not in buf:
            d = s.recv(4096)
            if not d:
                raise SystemExit("server closed mid-response")
            buf += d
        line, _, buf = buf.partition(b"\n")
        line = line.decode()
        if line == ".":
            return out
        out.append(line)
for stmt in ["SELECT name FROM people WHERE id = 3;",
             "SELECT * FROM nosuch;",
             "NOT SQL AT ALL;",
             "EXPLAIN SELECT name FROM people WHERE id = 3;"]:
    print("> " + stmt)
    for l in block(stmt):
        print("< " + l)
print("> QUIT")
for l in block("QUIT"):
    print("< " + l)
s.close()
PYEOF
    rc=$?
    [ "$rc" -eq 0 ] || { bad "server/raw: the raw-socket client exited $rc"; sed 's/^/      /' "$T/raw.err"; }
    # the exact block for a one-row SELECT, asserted as a unit
    cat > "$T/raw.want" <<'EOF'
> SELECT name FROM people WHERE id = 3;
< cols name
< row cy
< ok 1 row(s) from 1 examined
EOF
    head -4 "$T/raw.out" > "$T/raw.got"
    cmp -s "$T/raw.got" "$T/raw.want" || {
        bad "server/raw: the wire format is not what the protocol documents"
        diff "$T/raw.want" "$T/raw.got" | sed 's/^/      /'
    }
    # an in-band error must NOT end the session -- the statements after it were
    # answered, which is the whole point of reporting `err` instead of hanging up
    grep -qxF '< err no such table: nosuch' "$T/raw.out" || \
        bad "server/raw: an execution error did not come back in band"
    grep -qxF '< ok bye' "$T/raw.out" || \
        bad "server/raw: the session did not survive to QUIT after two failed statements"
    printf '=== server raw socket protocol\n' >> "$out"
    cat "$T/raw.out" >> "$out"

    # d -- rude clients must not take the daemon down
    python3 - "$port" >/dev/null 2>&1 <<'PYEOF'
import socket, sys
port = int(sys.argv[1])
# half a statement, then hang up
s = socket.create_connection(("127.0.0.1", port))
s.sendall(b"SELECT * FROM peo")
s.close()
PYEOF
    python3 - "$port" >/dev/null 2>&1 <<'PYEOF'
import socket, sys
port = int(sys.argv[1])
# a line that never ends, past the cap
s = socket.create_connection(("127.0.0.1", port)); s.settimeout(5)
try:
    s.sendall(b"A" * 20000)
    s.recv(4096)
except Exception:
    pass
s.close()
PYEOF
    # a THIRD client proves the daemon is still serving
    "$DB" "--client=127.0.0.1:$port" s2.sql > "$T/s3.out" 2> "$T/s3.err"
    rc=$?
    [ "$rc" -eq 0 ] || { bad "server/survives: the daemon stopped answering after a rude client (rc $rc)"; sed 's/^/      /' "$T/s3.err"; }
    srows 'server/survives' "$T/s3.out" '1|ada|36' '2|bob|41' '3|cy|29'
    grep -qF 'session ended: connection lost: closed mid-statement' "$T/srv.log" || {
        bad "server: a peer that hung up mid-statement was not reported as such"
        sed 's/^/      /' "$T/srv.log"; }
    grep -qF 'session ended: statement longer than 8192 bytes' "$T/srv.log" || {
        bad "server: the line cap did not fire on a 20000-byte line with no newline"
        sed 's/^/      /' "$T/srv.log"; }

    # e -- two clients at once. Serialised, so the second waits in the backlog;
    # both must still get the right answer.
    "$DB" "--client=127.0.0.1:$port" s2.sql > "$T/p1.out" 2>&1 &
    c1=$!
    "$DB" "--client=127.0.0.1:$port" s2.sql > "$T/p2.out" 2>&1 &
    c2=$!
    wait $c1; r1=$?
    wait $c2; r2=$?
    [ "$r1" -eq 0 ] && [ "$r2" -eq 0 ] || bad "server/serialised: concurrent clients exited $r1 and $r2, want 0 and 0"
    srows 'server/serialised c1' "$T/p1.out" '1|ada|36' '2|bob|41' '3|cy|29'
    srows 'server/serialised c2' "$T/p2.out" '1|ada|36' '2|bob|41' '3|cy|29'

    srv_stop
fi

# srv.SrvErr.Connect -- nothing is listening. The port just freed by srv_stop
# is the one port on this machine known to have no server on it.
printf 'SELECT 1;\n' > case.sql
"$DB" "--client=127.0.0.1:$port" case.sql > "$T/c.out" 2> "$T/c.err"
rc=$?
if [ "$rc" -eq 0 ]; then
    bad "Connect: EXITED 0 -- the client claimed to reach a server that is not there"
elif ! grep -qxF "cannot connect to 127.0.0.1:$port" "$T/c.err"; then
    bad "Connect: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
fi
[ -s "$T/c.out" ] && bad "Connect: wrote to STDOUT for a server it never reached"
printf '=== err Connect\n' >> "$out"
printf -- '--- stdout\n' >> "$out"; cat "$T/c.out" >> "$out"

# srv.SrvErr.Listen -- an address this host cannot bind. TEST-NET-3 (RFC 5737)
# is not assigned to any interface here, so bind(2) refuses it for root too,
# which a privileged-port fixture would not.
"$DB" --serve --host=203.0.113.1 --port=0 nl.db > "$T/c.out" 2> "$T/c.err"
rc=$?
if [ "$rc" -eq 0 ]; then
    bad "Listen: EXITED 0 -- an unbindable address reported success"
elif ! grep -qxF 'cannot listen on 203.0.113.1:0' "$T/c.err"; then
    bad "Listen: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
fi
printf '=== err Listen\n' >> "$out"
printf -- '--- stderr\n' >> "$out"; cat "$T/c.err" >> "$out"

# srv.SrvErr.Checkpoint -- the session runs, and the store it must be written
# to cannot be. Fatal by design: a server that took writes it cannot persist
# and kept accepting more would lose them all at exit.
[ -e nodir2 ] && bad "Checkpoint: nodir2 exists, the fixture does not test what it claims"
if srv_start nodir2/x.db; then
    printf 'CREATE TABLE t (a INT);\n' > s4.sql
    "$DB" "--client=127.0.0.1:$port" s4.sql > "$T/c.out" 2>&1
    # POLLED, NOT `wait`. A bare wait here is unbounded, so the one regression
    # this leg exists to catch -- a server that does not treat a failed
    # checkpoint as fatal -- would HANG the gate instead of reddening it. A
    # gate that can hang is worse than one that fails: nobody reads a log that
    # never arrives. Found by breaking exactly that, 2026-08-12.
    i=0
    while kill -0 "$SRVPID" 2>/dev/null && [ "$i" -lt 100 ]; do
        sleep 0.05
        i=$((i + 1))
    done
    if kill -0 "$SRVPID" 2>/dev/null; then
        bad "Checkpoint: the server was still running 5s after a checkpoint it could not perform -- the failure is not fatal"
        srv_stop
    else
        wait "$SRVPID" 2>/dev/null
        SRVPID=""
    fi
    grep -qF 'checkpoint failed: cannot write nodir2/x.db' "$T/srv.log" || {
        bad "Checkpoint: the server did not report the failed checkpoint with its own message"
        sed 's/^/      /' "$T/srv.log"; }
else
    bad "Checkpoint: the server never came up on an unwritable store path"
fi
printf '=== err Checkpoint\n' >> "$out"
grep 'checkpoint failed' "$T/srv.log" >> "$out"

# srv.SrvErr.Accept, which no client can provoke: it needs a broken LISTENING
# socket, and a gate that has one has a broken server, not a test. Probed
# through the API with a file descriptor that was never a socket.
P3="$T/pkg3"; mkdir -p "$P3"
for pkg in sql store exec wal plan srv; do
    cp -R "$src/$pkg" "$P3/" 2>/dev/null
done
cat > "$P3/probe3.ty" <<'EOF'
package main

import "srv"

fn main() -> Result(void, string):
    match srv.accept_one(-1):
        Ok(c): return Err("srv.accept_one ACCEPTED a connection on a non-socket")
        Err(e): return Err(srv.err_str(e))
EOF
if ! "$TYCHOC" "$P3/probe3.ty" -o "$T/probe3" >"$T/probe3.log" 2>&1; then
    bad "probe: tychoc could not build the Accept probe"
    sed 's/^/      /' "$T/probe3.log" | head -8
else
    "$T/probe3" > "$T/c.out" 2> "$T/c.err"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        bad "Accept: EXITED 0 -- srv.accept_one accepted on a non-socket"
    elif ! grep -qxF 'accept failed' "$T/c.err"; then
        bad "Accept: failed but not with its own message"; sed 's/^/      /' "$T/c.err"
    fi
    printf '=== err Accept (via the srv API)\n' >> "$out"
    cat "$T/c.err" >> "$out"
fi

# ---------------------------------------------------------------------------
# [6] the coverage floor: no variant may go ungated
#
# The five enums are READ, not remembered. A variant added to any of them
# without a leg above reddens here naming itself -- which is the only thing
# standing between "every error variant is covered" and "every error variant
# was covered on the day this was written".
#
# NotWritten is deliberately in this list once and covers both enums' variant
# of that name: store's is the failed store flush, wal's the failed log write,
# and the NotWritten leg above asserts BOTH messages from the one run.
# ---------------------------------------------------------------------------
COVERED='NoTable TableExists ColCount TypeClash Corrupt NotWritten NoIndex
         Storage NoColumn Uncomparable NotAValue NotAPredicate Plan
         Wal BadLog LostPrefix NotSelect
         Listen Connect Accept Peer TooLong Checkpoint'

variants() {
    awk -v want="enum $2:" '
        $0 == want { on = 1; next }
        on && /^[^ \t]/ { on = 0 }
        # A comment inside the enum body is not a variant. Without this the
        # scan yields "#" for every commented variant, which matches nothing
        # in COVERED and reddens the floor with a name that is not one.
        on && $1 ~ /^#/ { next }
        on && NF { v = $1; sub(/\(.*/, "", v); print v }
    ' "$1"
}

found=0
for v in $(variants "$src/store/store.ty" StoreErr) \
         $(variants "$src/exec/exec.ty" ExecErr) \
         $(variants "$src/wal/wal.ty" WalErr) \
         $(variants "$src/plan/plan.ty" PlanErr) \
         $(variants "$src/srv/srv.ty" SrvErr); do
    found=$((found + 1))
    hit=0
    for c in $COVERED; do [ "$v" = "$c" ] && hit=1; done
    [ "$hit" -eq 1 ] || bad "error variant $v has no leg in this runner -- it is UNGATED"
done
[ "$found" -ge 25 ] || bad "only $found error variant(s) found in the five enums -- the scan is broken, [4] asserts nothing"

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
    echo "tycho-db: green (demo transcript + store file + log reproducible over two runs; rows survive a process exit and a reopened store takes new writes; a real kill -9 mid-script replays to the completed rows only, idempotently, discarding a torn record; the index and scan paths return identical rows over every key while examining 1 against 6, and a constant-false WHERE examines 0 of 6; a real server answers over TCP on a kernel-chosen port, a second connection sees the first's writes, a raw socket gets the documented wire format, and two rude clients end their own sessions without stopping the daemon; $found error variants each exit non-zero with their own message, Corrupt and BadLog with empty stdout; transcript == golden)"
else
    echo "tycho-db: FAIL"; exit 1
fi
