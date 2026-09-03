set -eu
cd "$(dirname "$0")/.."
N="${1:-200}"
LANE="${2:-main}"
[ "$#" -le 2 ] || { echo "ci.sh: usage: scripts/ci.sh [FUZZ_N]" >&2; exit 2; }
# Fail-closed: a non-numeric FUZZ_N must abort, not silently skip the fuzz.
case "$N" in
    *[!0-9]*|"") printf 'ci.sh: FUZZ_N must be a non-negative integer, got "%s"\n' "$N" >&2; exit 2 ;;
esac
case "$LANE" in
    main|platform|corelib|apps|rest|fuzz-main|fuzz-reject|fuzz-leak) ;;
    *) printf 'ci.sh: unknown internal lane "%s"\n' "$LANE" >&2; exit 2 ;;
esac
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;;
    *) IS_WINDOWS=0 ;;
esac
case "$(uname -s):$(uname -m)" in
    Linux:x86_64|Linux:i?86) ILP32_HOST=1 ;;
    *) ILP32_HOST=0 ;;
esac

bar() { printf '================================================================\n'; }
step() { printf '\n>>> %s\n' "$1"; }
run_lanes() {
    pids=""
    for child in "$@"; do
        sh "$0" "$N" "$child" &
        pids="$pids $!"
    done
    failed=0
    for pid in $pids; do
        if ! wait "$pid"; then failed=1; fi
    done
    return "$failed"
}

if [ "$LANE" = main ]; then
ci_started=$(date +%s)
# Captured BEFORE the sweep, not after: a sha read fifteen minutes later names
# whatever the tree became, not what was tested. The fingerprint covers the
# uncommitted changes too, and is re-read at the end -- if it moved, nothing is
# recorded, because what was swept no longer exists.
ci_sha="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
ci_fp="$(sh scripts/status.sh --fingerprint)"
ci_skipped=""
bar
printf ' tycho local CI   (no GitHub Actions -- runs here, on this machine)\n'
printf ' fuzz seeds: %s\n' "$N"
bar

step "[1/13] build (make tychoc)"
make -s tychoc

step "[1a/13] make check-links  (relative links + every path:line citation resolves)"
make -s check-links

step "[1b/13] make goldens-check  (every golden a run.sh names is tracked by git -- the fresh-clone check)"
make -s goldens-check

step "[1c/13] make version-check  (every STATUS claim in a tracked doc names the version src/tychoc.c ships. The 0.6->0.7 bump grepped for \"Tycho 0.6\", missed \"Tycho is 0.6\", and left five files announcing the old release -- no lane here could see it, because the doc gates read links and citations and never a claim in a sentence. Scoped to status claims, not to the ~1080 version tokens in tracked Markdown, most of which are legitimate history)"
make -s version-check

step "[1d/13] make surface-check  (the language surface is frozen: keywords and builtins hard, corelib additive. It was enforced only when somebody typed it -- the freeze that exists because 91 commits touched docs/spec/ and 69 touched src/tychoc.c in ten days had no lane in the sweep. Adding a keyword or a builtin is meant to be a two-part act, the code plus a visible surface.lock diff a reviewer can refuse; without this step the second part was optional)"
make -s surface-check

step "[2/13] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

run_lanes platform corelib apps rest
if [ "$N" -gt 0 ]; then
    if [ "$IS_WINDOWS" = 1 ]; then
        step "[6/13] fuzz lanes skipped (Windows: the differential builds ASan binaries; mingw has no -lasan/-lubsan -- docs/internals/windows-port.md phase 2)"
        ci_skipped="fuzz(windows)"
    else
        run_lanes fuzz-main fuzz-reject fuzz-leak
    fi
else
    step "[6/13] fuzz lanes skipped (N=0)"
    ci_skipped="fuzz(N=0)"
fi

bar
printf ' CI GREEN -- tree is good (%ss)\n' "$(( $(date +%s) - ci_started ))"
# Record WHAT was verified, so `make status` can say whether this tree still is
# it. A green sweep whose content nobody wrote down answers "was the tree ever
# good", never "is it good now" -- and that gap is most of why the state of this
# project became unreadable. The record also carries the SCOPE: `make ci N=0`
# runs no fuzz lane, and a record that does not say so is a full sweep as far as
# any reader can tell.
if [ "$(sh scripts/status.sh --fingerprint)" != "$ci_fp" ]; then
    printf ' NOT RECORDED -- the tree changed while the sweep ran, so what was\n'
    printf ' tested no longer exists. Re-run on a settled tree.\n'
else
    mkdir -p build
    { printf 'tycho-ci-record v2\n'
      printf 'sha %s\n' "$ci_sha"
      printf 'fingerprint %s\n' "$ci_fp"
      printf 'date %s\n' "$(date '+%Y-%m-%d')"
      printf 'dur %s\n' "$(( $(date +%s) - ci_started ))"
      printf 'fuzz %s\n' "$N"
      printf 'skipped %s\n' "${ci_skipped:-none}"
    } > build/ci-status
fi
bar
exit 0
fi

if [ "$LANE" = platform ]; then
step "[2b/13] make ilp32  (fixture suite rebuilt under -m32: Tycho int stays 64-bit off LP64)"
if [ "$ILP32_HOST" = 0 ]; then
    echo "SKIP ilp32 (host is not Linux x86; int64 width is asserted by tests/int64_width.ty in step 2)"
else
    make -s ilp32
fi

step "[2c/13] make asan-self  (the COMPILER built with ASan+UBSan, compiling the whole corpus)"
if [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP asan-self (Windows: mingw ASan is experimental -- no -lasan/-lubsan; the compiler's own memory safety is the CI's job)"
else
    make -s asan-self
fi

step "[2d/13] make rtparity  (the emitted runtime surface -- env knobs, tycho: traps, arena-stats rows -- vs the oracle)"
make -s rtparity

step "[2e/13] make locale-check  (both locale fixtures compiled AND run under a comma-decimal LC_ALL forced by an LD_PRELOAD constructor)"
make -s locale-check
fi

if [ "$LANE" = corelib ]; then
step "[3/13] make corelib  (corelib packages + examples + the site/raytrace/mandelbrot/fetch/weblog/webserver dogfoods vs goldens)"
make -s corelib
make -s corelib-examples
make -s site
make -s raytrace
make -s mandelbrot
make -s fetch
make -s weblog
make -s webserver
fi

if [ "$LANE" = apps ]; then
step "[3b/13] make entrypoints  (every entry point in the tree still compiles)"
make -s entrypoints

step "[3c/13] make server-check  (tycho-httpd started for real: status codes, binary bodies, traversal, keep-alive, abuse suite, access log, SIGTERM)"
make -s server-check

step "[3d/13] make shim-check  (every corelib <pkg>_shim.c compiles standalone under -std=c11)"
make -s shim-check

step "[3d2/13] make shim-warn  (the shims' warnings, against a locked baseline)"
make -s shim-warn

step "[3d2/13] make source-bytes  (CRLF line endings and a NUL byte, both compilers, no fixture can carry them)"
make -s source-bytes

step "[3e0/13] make image-ceiling  (core:image: a 3.6 GB header from 69 bytes is refused, a real image still decodes)"
make -s image-ceiling

step "[3e/13] make ar-check  (tycho-ar: create twice byte-identical, t vs golden, diff -r round trip, damage and path traversal refused)"
make -s ar-check

step "[3f/13] make q-check  (tycho-q: 31-query transcript vs golden, select * byte-identical to the input, CSV == JSON, ten failure legs refused with empty stdout)"
make -s q-check

step "[3g/13] make vm-check  (tycho-vm: asm deterministic, dis round-trips byte-identically, listings + fib/gcd/sort output vs golden, trace deterministic, 7 runtime traps + 4 malformed sources refused with empty stdout)"
make -s vm-check
step "[3h/14] make scheme-check  (tycho-scheme: fib/closures/ho/sort vs golden byte-identically on two runs; 5 error cases die non-zero with empty stdout)"
make -s scheme-check
step "[3i/15] make kv-check  (tycho-kv: 3 command scripts byte-identical B+ tree vs map backend; reloads reproduce; golden locked)"
make -s kv-check
step "[3j/16] make chess-check  (tycho-chess: perft totals vs published values on start/kiwipete/pos3 + ep/promo/castling vs oracle; search deterministic + TT-invariant with exact tactical probes)"
make -s chess-check
step "[3k/17] make rsa-check  (tycho-rsa: textbook vector, 3 python-pow modexp sizes, Miller-Rabin probes incl. Carmichael 561, CSPRNG keygen invariants; RSAES-OAEP round trip through the CLI and a known-answer test vs python cryptography BOTH ways; the same plaintext encrypting twice to different ciphertexts, with a fixed-seed copy as the control)"
make -s rsa-check
step "[3l/18] make kvsrv-check  (tycho-kvsrv: HTTP KV round-trips + 405/404 + keep-alive + 4-way concurrent PUT/GET intact through the actor store)"
make -s kvsrv-check
step "[3m/19] make sat-check  (tycho-sat: PHP(2..9) all UNSAT; planted instances SAT with runner-verified models; deterministic; learning comparison recorded)"
make -s sat-check
step "[3n/20] make build-check  (tycho-build: first-build dispatch golden, second-build no-op differential, touch-rebuilds-only-dependents, failed recipe skips dependents, determinism, exit-2 errors)"
make -s build-check

step "[3o/21] make debug-check  (tycho-debug: scripted sessions -- breakpoint set + hit on the right source line, stripped-C-name locals, print, step, clean quit, run-to-completion with program output, Ctrl-C interrupt of a running inferior, fail-closed refusals, tycho debug wrapper)"
make -s debug-check

# 3p for the same reason 3e-3o are: a tool under tools/ that nothing else runs.
# tycho-db is the first program in the tree with its own internal packages, and
# a database has two ways to betray a caller -- return the wrong rows, or lose
# rows a process already acknowledged. The lane splits along that line: a golden
# carrying the ROWS plus a two-run determinism cmp of the transcript AND the
# store file, then a four-process persistence leg whose expected rows are
# literals in the runner rather than a slice of the golden. All eleven named
# error variants of store.StoreErr and exec.ExecErr must exit non-zero with
# their own whole message; the variant list is read out of the enums, so a new
# one cannot arrive ungated.
step "[3p/22] make db-check  (tycho-db: demo transcript + store file + log reproducible over two runs, rows survive a process exit and a reopened store takes writes, a real kill -9 mid-script replays idempotently and discards a torn record, the index and scan paths return identical rows while examining 1 against 6, a real server answers over TCP and survives rude clients, 25 error variants each refused with their own message)"
make -s db-check

# 3q for the same reason 3e-3p are: a tool under tools/ that nothing else runs.
# What makes it different from its neighbours is that its claims are about
# SCHEDULING, and a golden cannot see any of them -- a pipeline that lost its
# ordering, stopped being bounded, or races on the ring can all print the
# expected bytes on a kind run. So the lane varies the pool width and demands
# the same bytes, then proves the pool really does deliver out of order (a
# determinism claim over a pipeline that never raced is vacuous), then asserts
# the bounded ring against literals rather than the golden, then runs the whole
# thing under TSan -- because a capture bug here is a data race, which is right
# on this machine and wrong on the next.
step "[3q/23] make flow-check  (tycho-flow: transcript byte-identical over 8 runs and at TYCHO_THREADS=1 and 2, the pool proved to drain out of source order on 200 runs and not at all on one thread, a 4-slot ring that parks send 5, a cancelled pipeline that stops its source under 64 of 256 while the never-fails control runs to 256, 5 FlowErr variants and graph's cross-package collect each refused with their own message, TSan silent over the demo and 15 more pipelines)"
make -s flow-check

# 3r for the same reason 3e-3q are: a tool under tools/ that nothing else runs.
# What makes it different from its neighbours is that its subject is UTF-8, and
# a golden is structurally blind to a UTF-8 bug: a backspace that removes one
# BYTE of a 2-byte codepoint leaves a lone lead byte that renders as plausible
# text, and a golden re-recorded from that build diffs clean against it. So the
# byte and codepoint counts are asserted against literals in the runner, where
# RECORD=1 cannot bless them, and the undo journal is closed into a loop --
# undone to an empty buffer and redone back to a byte-identical dump -- which
# demo.ed does not do.
step "[3r/24] make ed-check  (tycho-ed: demo transcript byte-identical over 2 runs, a backspace over a 2-byte codepoint asserted at 13->11 bytes and 11->10 codepoints and a forward delete of a 3-byte one at 19->16 and 13->12 against literals, no dump reporting INVALID UTF-8, 6 edits undone to an empty buffer and redone to a byte-identical dump, 7 BufErr variants each refused with their own message)"
make -s ed-check

step "[3s/25] make sheet-check  (tycho-sheet: demo transcript byte-identical over 2 runs, 98411 generated floats rendered and read back bit-equal with 0.1+0.2, 2^53, DBL_MAX and the min subnormal each asserted separately and none falling back to #NUM!, str(0.1+0.2) round-trips, a cycle NAMED F1 -> F2 -> F3 -> F1 and a self-reference G1 -> G1, 10000- and 100000-deep chains exact and four depth limits past them failing closed by name, 13 of 14 CellErr/ParseErr variants each exiting non-zero with their own whole message and the 14th proved unconstructible)"
make -s sheet-check

step "[3t/26] make sim-check  (tycho-sim: demo, sweep and stale transcripts each byte-identical over 2 runs, 12 of 24 entities surviving a scripted despawn sweep with the count computed in the runner and every survivor resolved through its id to the hp it was spawned with, a despawned id refused as dead and the same id refused as stale once its slot is handed on with the generation moved, 3 SimErr variants each refused with their own whole message)"
make -s sim-check

step "[3u/27] make make-check  (tycho-make: demo report byte-identical over 2 runs, 8 nodes ordered by DECLARATION order where alphabetical would disagree, every printed edge respected with each node listed exactly once, one edge removed from a 4-node chain moving the order, 3 cycles NAMED including a self-edge and one with innocent nodes stuck behind it, 8 MakeErr variants each exiting non-zero with their own whole message and an empty stdout; then the executor: a cold build runs all 4 rules with zeta.o and alpha.o really running before app in the recipes' own trace, a no-op rebuild runs ZERO, changing one input reruns exactly its 2 dependents, moving a file's mtime with its bytes intact reruns NOTHING and calls it touched, a chain node starting before the wide level beside it finishes -- the leg the wavefront this replaced cannot pass -- the log is byte-identical over 6 runs at TYCHO_THREADS 1/2/8, and 6 BuildErr variants are accounted for)"
make -s make-check

step "[3u2/27] make diff-check  (tycho-diff: unified output == golden and the diff(1) 0/1/2 exit contract held on a differing pair, an identical pair and 5 error paths each with an empty stdout; over 206 generated pairs -- including two empty files, one empty, identical and a reversal -- every edit script rebuilt BOTH files exactly and matched GNU diff's edit distance, which is what a transcript cannot see)"
make -s diff-check

step "[3u3/27] make hash-check  (tycho-hash: the report byte-identical at 1/2/3/5/8 workers AND the pool proven to share -- all 8 take a file at width 8 while at width 1 the first takes all 12 and the rest none, which is the negative control for --workers; every hash equals sha256sum's; the per-worker counts sum to exactly the file count at every width; 7 error paths exit 2 with an empty stdout)"
make -s hash-check

step "[3u4/27] sh scripts/fuzz_shims.sh  (fuzz the corelib paths that take UNTRUSTED BYTES -- compress.decompress and regex.compile/is_match -- under ASan+UBSan. fuzz/run.py targets the COMPILER; nothing fuzzed the shims a running program feeds attacker data to. A deliberate heap overflow must be caught FIRST, because a fuzzer reporting zero findings is indistinguishable from one that is not running)"
sh scripts/fuzz_shims.sh

step "[3u5/27] make fold-check  (tycho-fold: over 200 generated lines mixing ASCII/Latin-1/CJK/emoji at widths 3..30, nothing is lost, no line exceeds the width in CODEPOINTS and every line stays valid UTF-8 -- and the byte-counting mode agrees on pure ASCII while differing on non-ASCII, which is what makes those three mean anything)"
make -s fold-check

step "[3u6/27] sh scripts/bignum_diff.sh  (differential core:bignum AND core:decimal against Python's integers and Decimal -- an INDEPENDENT arbitrary-precision implementation. make corelib checks it against a golden, which proves it has not CHANGED, not that it was ever right. A control scores division by the WRONG (floor) model first and must find mismatches, because a clean differential with a dead comparison is indistinguishable from a correct one)"
sh scripts/bignum_diff.sh

step "[3u7/27] sh scripts/crypto_hygiene.sh  (does core:crypto leave secret material in memory it has RELEASED? make corelib checks the ANSWERS -- that a ciphertext decrypts, that a signature verifies -- and every one of those passes whether or not the plaintext is still sitting in a freed heap block, because hygiene has no output. --wrap=free interposes only the shim's own frees, so a hit names that file; two controls run first, a dirty block that must be FOUND and a cleansed one that must NOT be)"
sh scripts/crypto_hygiene.sh

step "[3u8/27] sh scripts/tls_verify.sh  (does core:tls actually VERIFY the certificate? corelib/test/tls connects to a CLOSED PORT, so it cannot tell a refused CERTIFICATE from a refused CONNECTION -- both give a null handle, and SSL_VERIFY_NONE passed every gate here. A real TLS server on a kernel-chosen loopback port with an untrusted cert: the untrusted chain must be refused, the SAME server accepted once its CA is trusted and reached by the name in the cert, and refused again by a name the cert does not carry. The middle leg is what stops the other two passing on a dead connection)"
sh scripts/tls_verify.sh

step "[3u8b/27] sh scripts/http_verify.sh  (the same question for core:http, which is the HTTPS client every Tycho program actually reaches for -- and it could not be asked until now. The three-way check was built for it on 2026-08-15 and REMOVED, because its positive control was unreachable: CURL_CA_BUNDLE is read by the curl TOOL not by libcurl, SSL_CERT_FILE was pre-empted by libcurl's compiled-in CAINFO, and the shim exposed neither, so no leg could be made to SUCCEED and a CURLOPT_SSL_VERIFYPEER 0L added while debugging would have passed every lane here. The shim honours SSL_CERT_FILE/SSL_CERT_DIR now, so the untrusted chain must be refused, the SAME server accepted once its CA is trusted by EITHER variable and reached by the cert's name, refused again by a name it does not carry, and a control built against a COPY with verification off must ACCEPT what leg 1 refused)"
sh scripts/http_verify.sh

step "[3u9/27] sh scripts/format_diff.sh  (core:csv against Python's csv module and core:json against Python's json. make corelib checks both against goldens THIS REPO RECORDED, which proves they have not changed and not that they were ever right -- and for a format whose whole point is that somebody else reads it back, agreeing with its own earlier self is the wrong property. It found one: csv.stringify's stated parse(stringify(rows)) == rows was false for a row of ONE EMPTY FIELD, 413 of 414 row-sets being fine is why no golden noticed)"
sh scripts/format_diff.sh

step "[3u9b/27] sh scripts/math_diff.sh  (core:math against Python's arithmetic. Both defects this 75-line package has had were found by READING a claim -- gcd's documented non-negative (FRICTION #64) and sign's documented -1/0/1 (FRICTION #65) -- and both were invisible here, because make corelib compares core:math to a golden THIS REPO RECORDED, which agrees with whatever the code did the day it was written. Where the semantics genuinely differ the oracle encodes the DOCUMENTED tycho answer and says so: gcd(min,0) is 2^63 and does not fit, so it follows abs() in returning min, and an ipow whose true answer leaves int64 is SKIPPED by design with the skip count printed. The float arm is the load-bearing one: the int-only first version scored 1197 clean answers while the sign-of-infinity defect sat in front of it, because min/max/clamp/sign are generic and their int instantiation says nothing about their float one. NaN is scored for sign alone -- min/max on a NaN is a branch order artefact, not a documented answer)"
sh scripts/math_diff.sh

step "[3u9c/27] sh scripts/traversal_depth.sh  (server/run.sh proves a path traversal is REFUSED; it cannot say by WHICH guard. server/main.ty@resolve has two independent ones -- hidden_segment(path.clean(rel)) and path.safe_join(root, rel) -- and either alone refuses every payload, so DELETING ONE CHANGES NO OBSERVABLE BEHAVIOUR and every gate here stays green while the defence halves. Two commits neither of which looks wrong take it to zero. This defeats them one at a time in a COPY of the server and requires the refusal to hold; the control defeats BOTH and must see the canary leak, without which legs 1-3 all pass on a probe that never reached the server -- the same blindness that made core:http ungatable until FRICTION #57)"
sh scripts/traversal_depth.sh

# tycho-snap: the newest tool lane, and the only one whose subject is an archive
# read back by SOMEBODY ELSE'S implementation -- our own CRC over our own bytes
# proves nothing about interoperability.
step "[3v/28] make snap-check  (tycho-snap: transcript AND archive bytes identical over 2 runs, the 4-member entry set and its sorted ORDER exact against literals with sub/ descended, .txt filtered and skipme/ never entered, python3 zipfile testzip() None and a member's sha256 out of the archive equal to the file on disk, an empty selection a 22-byte EOCD read as 0 entries, a missing manifest exit 1 naming the file and an unknown option exit 2 naming it)"
make -s snap-check

# tycho-tally: the SQLite ledger, and the only lane whose subject is a TEST
# FRAMEWORK -- its control breaks an assertion in a copy and requires the suite
# to notice.
step "[3w/29] make tally-check  (tycho-tally: a 15-check core:testing suite passing twice byte-identically AND proven able to fail -- a copy with one expected total changed exits 1 naming the check and counting 1 of 15; three processes write the ledger and a fourth reads it back against literals, SQL doing the sort and the SUM; a non-numeric amount exits 2 and books nothing)"
make -s tally-check

step "[3x/30] make agg-check  (tycho-agg: report identical over 2 runs and equal to the golden; north 3 / south 2 / east 1 against literals with an empty-key row dropped by the generic filter; --min filters the display without moving distinct=3; the emitted C carries pipe__keep__, pipe__to__ and pipe__group_into__ instantiated at the program's own Row type; missing file, absent column and unknown option each exit non-zero naming the thing)"
make -s agg-check

# tycho-tmpl: the only program using `sink`, and the only lane asserting that a
# consume rule still REFUSES four shapes.
step "[3y/31] make tmpl-check  (tycho-tmpl: render identical over 2 runs and equal to the golden; substitution against literals with a repeated key; all four sink shapes -- accumulate in a loop, collect then consume, create-grow-consume, count before consuming -- still refused with a sink diagnostic; a missing key exits 1 naming the placeholder; an unknown option refused by name)"
make -s tmpl-check

# tycho-stat: the only consumer of variadics and `zero$(T)` outside tests/, and
# the lane that checks the ANSWERS rather than reproducing them.
step "[3z/32] make stat-check  (tycho-stat: run identical over 2 runs and equal to the golden; count/sum/min/max/mean over a 201-number corpus each equal to the runner's own arithmetic; both empty identities exactly 0 from zero\$(T); negatives survive parsing and min; a non-numeric field refused by name rather than read as its leading digits; an empty generic variadic naming no type still refused with a message naming the cure; an unknown option refused by name)"
make -s stat-check

# tycho-ledger: the only program using newtypes ACROSS a package boundary, and
# the lane whose load-bearing leg is five refusals no transcript can show.
step "[4a/33] make ledger-check  (tycho-ledger: run identical over 2 runs and equal to the golden; totals and the --rate doubling against literals; all five distinctness violations still refused, each naming an UNMANGLED type; keys() hands back wrapped keys that index the map with no unwrap; a non-numeric amount and an unknown option each refused by name)"
make -s ledger-check

step "[4b/34] make fh-check  (tycho-fh: run identical over 2 runs and equal to the golden; counts and balance against literals; live=0 and opens==closes after 64 scope exits and again after 20000 with a re-read checksum; all six affine shapes -- decl copy, bare handle struct field, returned, in an array, in an Option, close() on a call result -- refused; a double borrow still leaves one live owner; an unknown option refused by name)"
make -s fh-check

# tycho-grid: the second consumer of subscript, bounded[N]T and the deprecation
# marker -- the three features that had one real user each.
step "[4c/35] make grid-check  (tycho-grid: run identical over 2 runs and equal to the golden; the subscript as a place and an rvalue against literals with two independently-computed totals agreeing; the deprecation warning emitted with its text, NOT emitted for prose that merely mentions the marker, and emitted for a fn taken as a value; a fifth mark past bounded[4] exits non-zero naming the limit; all five subscript rules plus the flat 2-D once-per-parameter limit still refused; a bounded struct field still copies by value)"
make -s grid-check
fi

if [ "$LANE" = rest ]; then
step "[4/13] make conc  (spawn/parallel-for/channels: native + ASan + TSan vs goldens)"
make -s conc

step "[5/13] make ffi  (extern fn vs golden, ASan-clean, handle/injection bans)"
make -s ffi
fi

if [ "$LANE" = fuzz-main ]; then
    step "[6/13] make fuzz N=$N  (random programs: tychoc native -O2 vs tychoc ASan/UBSan)"
    python3 fuzz/run.py "$N"
fi
if [ "$LANE" = fuzz-reject ]; then
    step "[7/13] make fuzz-reject N=$N  (malformed input: tychoc must fail closed)"
    python3 fuzz/run_reject.py "$N"
fi
if [ "$LANE" = fuzz-leak ]; then
    # Leak bugs surface fast, so cap this sequential lane; make fuzz-leak runs deeper.
    LN="$N"; [ "$LN" -gt 150 ] && LN=150
    step "[8/13] make fuzz-leak N=$LN  (LeakSanitizer: arena / owner-0 leaks)"
    python3 fuzz/run_leak.py "$LN"
fi

if [ "$LANE" = rest ]; then
step "[9/13] make tools-check  (formatter idempotence + semantic preservation + LSP smoke)"
sh scripts/tools_check.sh

step "[9b/13] make editors-check  (zed grammar: src/ still generated from grammar.js, corpus still parses; vscode JSON is JSON)"
make -s editors-check

step "[10/13] bench-guard  (tree-alloc wall: tycho must beat C -- perf regression gate)"
sh bench/guard.sh

step "[11/13] make recursion  (deep input fails closed -- no stack-overflow DoS)"
make -s recursion

step "[12/13] make spec-check  (spec: Appendix A grammar == §3/§4 · Appendix E fixtures exist · runnable examples match their documented output)"
make -s spec-check

step "[12b/13] make docs-fences  (every fenced tycho block in docs/ that claims to be a whole program still compiles)"
make -s docs-fences

step "[13/13] make check-links  (every relative Markdown link resolves to a real file; every provenance citation still resolves)"
make -s check-links
fi
