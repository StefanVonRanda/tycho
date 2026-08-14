# Tycho compiler build.
#
#   make            -> build ./tychoc (native, macOS/Linux host)
#   make demo       -> compile + run examples/hello.ty natively
#   make clean

CC      ?= cc
# -fwrapv: signed integer overflow wraps (two's complement), never C UB -- the
# compiler's own arithmetic must honour the same overflow contract it gives
# generated programs (see the -fwrapv on the codegen cc line in src/tychoc.c).
CFLAGS  ?= -O2 -fwrapv -Wall -Wextra -std=c11

EMBED   := build/tycho_rt_embed.h
RUNTIME := runtime/tycho_rt.c

.PHONY: all tools tools-check demo test test-fast prunner test-update conc rtparity bench bench-prongB bench-dbquery bench-conc bench-indexer bench-window bench-latency bench-gcscan bench-guard bench-site fuzz fuzz-quick fuzz-reject fuzz-leak corelib corelib-examples shim-check goldens-check ar-check build-check debug-check q-check vm-check scheme-check kv-check db-check flow-check ed-check sheet-check sim-check make-check snap-check tally-check agg-check tmpl-check stat-check ledger-check fh-check grid-check chess-check rsa-check kvsrv-check sat-check locale-check fetch weblog webserver site raytrace mandelbrot ffi recursion entrypoints spec-check docs-fences check-links server server-check wiki ci release-check hooks ilp32 asan-self editors-check clean

all: tychoc

# Turn the runtime C source into a single C string literal the compiler
# embeds into every generated file. Each line is escaped and suffixed
# with \n so the emitted C is byte-for-byte the runtime source.
$(EMBED): $(RUNTIME) | build
	@awk 'BEGIN{print "static const char *TYCHO_RUNTIME ="} \
	     {gsub(/\\/,"\\\\"); gsub(/"/,"\\\""); printf "\"%s\\n\"\n",$$0} \
	     END{print ";"}' $(RUNTIME) > $(EMBED)

build:
	@mkdir -p build

tychoc: src/tychoc.c $(EMBED)
	$(CC) $(CFLAGS) -Ibuild src/tychoc.c -o tychoc

# The `tycho` daily-driver CLI (run/build/check/watch) -- itself a Tycho program,
# built with tychoc + the FFI shell-out shim. Run as `./tycho <cmd> <file.ty>`
# (set TYCHOC=./tychoc to use the in-repo compiler). See tools/tycho.ty.
tycho: tychoc tools/tycho.ty tools/tycho_shim.c
	./tychoc tools/tycho.ty --shim tools/tycho_shim.c -o tycho

# tychofmt -- the source formatter. Lossless, comment-preserving lexer + canonical
# pretty-printer: re-indents, re-spaces by token adjacency, groups top-level defs.
# Whitespace-only (emit-C identical, verified by tools-check). See tools/tychofmt.ty.
tychofmt: tychoc tools/tychofmt.ty
	./tychoc tools/tychofmt.ty -o tychofmt

# tycho-lsp -- the language server (JSON-RPC over stdin/stdout, dogfooded in tycho).
# STAGE 1: lifecycle handshake; stage 2 adds diagnostics. Any LSP client drives it.
tycho-lsp: tychoc tools/lsp.ty tools/lsp_shim.c
	./tychoc tools/lsp.ty --shim tools/lsp_shim.c -o tycho-lsp

# tycho-debug -- the gdb adapter (compiles with -g, drives a gdb MI session).
# A standalone package (tools/tycho-debug/) because importing core:* needs a
# package header; its spawn/pipe/signal companion is tools/tycho-debug/debug_shim.c.
tycho-debug: tychoc tools/tycho-debug/main.ty tools/tycho-debug/debug_shim.c
	./tychoc tools/tycho-debug/main.ty --shim tools/tycho-debug/debug_shim.c -o tycho-debug

# build the whole daily-driver toolchain (driver + formatter + language server + debugger)
tools: tycho tychofmt tycho-lsp tycho-debug

# regression guard for the tooling: formatter idempotence + semantic preservation
# (emit-C identical before/after) and an LSP JSON-RPC smoke test. Part of `make ci`.
tools-check: tychoc
	@sh scripts/tools_check.sh

# Regression guard for the two editor grammars: the committed tree-sitter src/ is
# a GENERATED artifact and must still match grammar.js, the generated parser must
# still parse the whole .ty corpus, and the vscode JSON must still be JSON.
# Nothing else in the tree looks at editors/. See scripts/editors_check.sh.
editors-check:
	@sh scripts/editors_check.sh

# Entry-point sweep: every program in the tree with an entry point must still
# COMPILE (`tychoc --emit-c`, so no cc, no link, no external library). Exists
# because `make ci` builds no example that has its own runner, which is how
# examples/webserver/main.ty stayed uncompilable for a whole phase with nothing
# red. Milliseconds. Part of `make ci`. See scripts/entrypoints.sh.
entrypoints: tychoc
	@sh scripts/entrypoints.sh

# Spec consistency gate: the collected grammar (spec Appendix A) must stay
# byte-identical to the defining chapters §3/§4. See scripts/spec_check.sh.
spec-check:
	@sh scripts/spec_check.sh

# Front-end pass over the ```tycho fences in docs/. spec_examples.sh only builds
# a fence PAIRED with an ```output block (9 in the whole spec); everything else
# was unparsed prose, which is how a snippet using a digit separator the language
# does not have sat in docs/guides/ for months. Skips are named, not silent --
# see the header of scripts/docs_fences.sh for exactly what is NOT covered.
docs-fences: tychoc
	@sh scripts/docs_fences.sh

# Doc hygiene: every relative Markdown link points at a file that exists, and
# every `path:N` provenance citation still resolves (bounds for a bare citation,
# the named symbol's extent for an anchored `path:N@symbol` one — see the header
# of scripts/check_citations.py for exactly what that does and does not catch).
check-links:
	@sh scripts/check_links.sh
	@python3 scripts/check_citations.py --selfcheck
	@python3 scripts/check_citations.py

# Regenerate the GitHub wiki's reader-doc mirror from /docs. Clones or pulls the
# wiki repo into ./.wiki (gitignored), runs the sync, and audits the links. Does
# NOT push — review ./.wiki, then commit and push it yourself.
WIKI_DIR ?= .wiki
WIKI_REMOTE ?= https://github.com/StefanVonRanda/tycho.wiki.git
wiki:
	@if [ -d $(WIKI_DIR)/.git ]; then git -C $(WIKI_DIR) pull --ff-only; \
	else git clone $(WIKI_REMOTE) $(WIKI_DIR); fi
	@python3 scripts/sync-wiki.py $(WIKI_DIR)
	@echo "Review $(WIKI_DIR)/, then: git -C $(WIKI_DIR) add -A && git -C $(WIKI_DIR) commit -m ... && git -C $(WIKI_DIR) push"

demo: tychoc
	./tychoc examples/hello.ty
	@echo "--- running examples/hello (type a name) ---"
	@./examples/hello

# wine-smoke: the manual Linux-box verification lane for the native-Windows port
# (plan_windows.md). Cross-compiles selected fixtures with the mingw-w64 compiler
# and runs them under Wine against the Linux goldens -- concurrency, timers,
# floats, list_dir, and the stack-overflow guard. NOT a gate, and NOT a Windows
# verdict: Wine is an approximation; the definitive pass is the windows CI leg.
# Skips loudly when mingw/wine are absent. See scripts/wine_smoke.sh.
wine-smoke:
	@sh scripts/wine_smoke.sh

# wine-test: phase 3's manual lane — the whole fixture corpus (plain + pkg +
# abort + diag) cross-compiled and run under Wine against the Linux goldens.
# NOT a gate, NOT a Windows verdict; its value is the park list (fixtures that
# redden only for Windows-environment reasons, audited in phase 6).
wine-test:
	@sh scripts/wine_test.sh

# wine-corelib: phase 4's manual lane — every corelib test that can link on
# this box, cross-compiled under mingw and run under Wine vs the Linux goldens.
# NOT a gate, NOT a Windows verdict; skips carry their reasons (MSYS2-only
# library links and POSIX-only test mechanisms are parked for phase 6).
wine-corelib:
	@sh scripts/wine_corelib.sh

# wine-tools: phase 5's manual lane — every tools/ tool cross-compiled under
# mingw (the Windows build proof) plus representative runs under Wine. NOT a
# gate, NOT a Windows verdict; the full lane fixture suites are CI's.
wine-tools:
	@sh scripts/wine_tools.sh

# wine-ffi: phase 6's manual lane — the FFI lane's runnable legs under mingw
# + Wine (golden, --shim, pkgext, sized ints, the compiler-side rejections).
# NOT a gate, NOT a Windows verdict; the ASan leg is the CI's.
wine-ffi:
	@sh scripts/wine_ffi.sh

# Differential test suite: every examples/*.ty and tests/*.ty built both native
# -O2 and under ASan/UBSan, run on matching stdin, and scored by shell/cmp/grep.
# Positive fixtures use a bounded worker pool; TYCHO_THREADS=1 is the sequential
# oracle and RECORD=1 stays sequential. See tests/run.sh and docs/thesis.md §3.
test: tychoc
	@sh tests/run.sh

# The same corpus through a Tycho-written judge. Kept as concurrency dogfood,
# not authority: a compiler regression can land inside its own judge. `test`
# above remains the answer to believe; both use TYCHO_THREADS=N for width.
build/prunner: tools/prunner/main.ty tychoc | build
	@./tychoc tools/prunner/main.ty -o build/prunner

prunner: build/prunner

test-fast: build/prunner
	@./build/prunner

# The COMPILER's own memory safety. Every other sanitizer lane here (including
# `test` above) sanitizes the C tychoc EMITS; nothing built src/tychoc.c itself
# with -fsanitize, so tychoc's own execution was unmeasured by every gate -- which
# is how the parse_type_inner stack-buffer-overflow of the front-door plan survived a
# full 1.0 freeze. This lane builds src/tychoc.c with ASan+UBSan and COMPILES the
# whole fixture corpus with it (--emit-c; the emitted programs are not run -- that
# is `test`'s job). Leak detection is deliberately off: tychoc never frees by
# design. Rationale, coverage, and what is NOT covered: scripts/asan_self.sh.
asan-self: $(EMBED)
	@sh scripts/asan_self.sh

# Concurrency suite (spawn/wait, parallel for, channels): tychoc builds each
# positive fixture native + ASan/LSan + TSan against the goldens, rejects must
# fail, aborts must die with their .err message. In `make ci`.
conc: tychoc
	@sh tests/conc/run.sh

# Runtime-surface lane: compile tests/rtparity/surface.ty and check the emitted C
# still contains every env knob, "tycho: ..." trap text and arena-stats row the
# oracle in tests/rtparity/run.py records -- both directions, so a lost trap and
# an unrecorded new one both fail. Until 2026-07-29 this compared tychoc's
# runtime against the one frozen tychoc0 emitted; that leg is gone. Its docstring
# advertised `make rtparity` from the day it was written and no such target
# existed, so nothing in the tree ran it. Under a second (~1s: one --emit-c, no
# cc). In `make ci` as step [2d/13] since 2026-07-30 (the loops-cleanup plan) -- a
# tests/ lane, so a sub-lane of step 2 rather than a new number. It paid for
# itself on the way in: it was the only gate that saw phase 53 remove the
# "tycho: range step is zero" trap from the emitted C.
rtparity: tychoc
	@python3 tests/rtparity/run.py

# locale-check: the two locale fixtures, compiled AND run with the process
# locale actually hostile. A sub-lane of step 2 for the same reason rtparity is
# -- it consumes tests/*.out -- and it is in `make ci` as step [2e/13].
#
# WHAT ONLY THIS LANE CAN SEE. Three locale fixes landed across three plans and
# NOTHING gated any of them, because each is latent until something in the
# process calls setlocale, and neither tychoc nor the runtime ever does:
#
#   src/tychoc.c@c_strtod           the lexer READing a float literal
#   src/tychoc.c@c_dtoa             codegen WRITing one
#   runtime/tycho_rt.c@tycho_float_to_str   a running program's str(float)
#
# The third is already covered by `make test`: tests/float_str_locale.ty flips
# LC_NUMERIC inside its own process through a runtime test hook, so the grader's
# environment does not matter. The first two are NOT, and cannot be by a fixture:
# tychoc is a separate process that has exited before main() runs, and setting
# LC_ALL for it is INERT -- a C program starts in "C" whatever the environment
# says (the four-found plan ran the fully broken
# compiler under LC_ALL=da_DK.utf8 and it came out green). Reaching them needs a
# load-time constructor calling setlocale, which is what this lane LD_PRELOADs.
#
# It builds its preload in a mktemp -d, writes nothing into the tree, and skips
# loudly by name -- with a reason, exit 0 -- on a host with no comma-decimal
# locale, no locale(1), or an LD_PRELOAD that does not take effect. ~1.5s
# (1.73 / 1.46 / 1.44 s, measured 2026-08-02).
# See scripts/locale_check.sh.
locale-check: tychoc
	@sh scripts/locale_check.sh

# Re-record the expected-output goldens (tests/*.out) from current output.
# Opt-in only: a normal `make test` never writes them, so a regression cannot
# silently rebake itself into the expected files. Review `git diff tests/`.
test-update: tychoc
	@RECORD=1 sh tests/run.sh

# Performance guard: assert the thesis's optimizations still hold (peak RSS
# stays linear, the inout memo stays O(n)). See bench/run.sh.
# `entrypoints` first (0.2s): it compiles every .ty under bench/, so a language
# change that broke a benchmark says so here instead of part-way through a run.
bench: tychoc entrypoints
	@sh bench/run.sh

# Head-to-head memory benchmark: the same workloads in Tycho / C / Rust / Go /
# Koka, peak RSS + wall time + output-identity, with a normalized scorecard.
# The empirical half of the thesis. See bench/prongB/run.sh and RESULTS.md.
bench-prongB: tychoc
	@sh bench/prongB/run.sh

# Real-library head-to-head: the same SQLite workload in tycho / C / Go (peak RSS
# + wall + identical checksum). Needs libsqlite3; skips absent toolchains. NOT in
# `make ci` (system dependency). See bench/dbquery/RESULTS.md.
bench-dbquery: tychoc
	@sh bench/dbquery/run.sh

# Concurrency head-to-head (parallel reduce + channel pipeline) vs C/Go/Rust.
bench-conc: tychoc
	@sh bench/conc/run.sh

# Parallel text indexer dogfood (channel fan-out -> worker maps -> inout merge)
# vs C/Go over an identical synthetic corpus. See bench/indexer/RESULTS.md.
bench-indexer: tychoc
	@sh bench/indexer/run.sh

# Static-site generation: render N Markdown pages to HTML, tycho vs C vs Go,
# peak RSS + wall. tycho's per-scope arena keeps memory FLAT across a 20x scale
# (matches C, no manual free) where Go's GC holds garbage; an FNV checksum of
# every rendered byte gates fairness. See bench/site/RESULTS.md.
bench-site: tychoc
	@sh bench/site/run.sh

# Sliding-window eviction: the arena's weak point, mapped honestly (heap-record
# window loses ~14x; fixed-size ties). tycho vs C vs Go. See bench/window/RESULTS.md.
bench-window: tychoc
	@sh bench/window/run.sh

# Latency / GC-pause predictability: tycho/C pause-free, Go's GC pause measured.
# See bench/latency/RESULTS.md.
bench-latency: tychoc
	@sh bench/latency/run.sh

# Large held set: per-object overhead (tycho most compact) + GC-scan cost (Go's
# memory-vs-CPU tradeoff under a GOGC sweep; tycho/C never scan). bench/gcscan/RESULTS.md.
bench-gcscan: tychoc
	@sh bench/gcscan/run.sh

# corelib: the standard library (packages under corelib/, imported as `core:<name>`,
# resolved via TYCHO_CORELIB). Each corelib/test/<name> must compile + run and match
# its recorded golden. See corelib/run.sh.
corelib: tychoc
	@sh corelib/run.sh

# corelib examples: a small, readable program per core module (usage as
# documentation, not assertions like corelib/test/), golden-validated like the
# tests, with the same deps-skip. See examples/corelib/run.sh.
corelib-examples: tychoc
	@sh examples/corelib/run.sh

# shim-check: every corelib <pkg>_shim.c must compile ON ITS OWN under -std=c11.
# The real build never compiles one alone -- tychoc appends it to the generated .c
# on a single cc line with no -std flag -- so a shim that relies on a header some
# earlier source pulled in, or on the default dialect's implicit _DEFAULT_SOURCE,
# is invisible to every other gate. Needs no tychoc: it is cc over 12 files, <1s.
# See scripts/shim_check.sh for why the flags differ from the build's.
shim-check:
	@sh scripts/shim_check.sh

# goldens-check: every golden a `run.sh` names is tracked by git. `.gitignore`
# ignores *.out broadly and un-ignores per directory, one line per lane, so a new
# lane's recorded golden is green here and absent from a fresh clone --
# tools/tycho-ar/expected.out shipped that way once and was caught by hand. Needs
# no tychoc and no build: it is `git ls-files` over a text scan of the runners,
# ~0.07s. See the header of scripts/check_goldens.py for what the scan follows,
# the one gap it does not, and why it floors per lane instead of counting.
goldens-check:
	@python3 scripts/check_goldens.py

# ar-check: the gate for tycho-ar, the deterministic archiver in
# tools/tycho-ar/. A batch program, so it gates with a golden the way the
# examples/ dogfoods do rather than with a daemon harness the way server-check
# does: build it, run it over a fixture the runner writes itself, and compare
# `t`'s listing against a recorded golden. On top of the golden it asserts the
# four properties an archiver cannot be trusted without -- create-twice
# byte-identical, `diff -r` round trip empty, damage refused (flipped payload,
# forged payload digest, sheared trailer), and a member path that escapes the
# destination refused before the first write.
#
# It is the only lane that RUNS tycho-ar. Its compilation was already covered --
# scripts/tools_check.sh sweeps every .ty in the tree and does `--emit-c` on each
# for the formatter's semantic-preservation leg, so a syntax error there reddens
# step [9] -- but scripts/entrypoints.sh globs examples/*/ plus server/main.ty and
# never looks under tools/, and nothing anywhere executed the binary. So the
# archiver could have stopped being deterministic, stopped round-tripping, or
# started extracting through `../` with every gate green. ~2.4s, measured
# 2026-07-31. In `make ci` as step [3e/13].
# See tools/tycho-ar/run.sh.
ar-check: tychoc
	@sh tools/tycho-ar/run.sh

# build-check: the gate for tycho-build, the make-like build tool in
# tools/tycho-build/. Same shape as ar-check -- a batch program gates against a
# recorded golden plus behavior legs: first-build dispatch order, the
# second-build no-op differential, touch-rebuilds-only-dependents, a failed
# recipe skipping its dependents, determinism, and exit-2 errors.
build-check: tychoc
	@sh tools/tycho-build/run.sh

# debug-check: the gate for tycho-debug, the gdb adapter in tools/tycho-debug/.
# Same shape and same reasoning as ar/q/vm-check above -- a batch-ish program,
# so it gates behaviorally rather than with a golden (the transcript includes
# gdb's own output, which drifts across gdb versions): build the tool, run
# scripted sessions over fixtures the runner writes, and assert breakpoint
# set + hit on the right source line, stripped-C-name locals, print, step,
# clean quit, run-to-completion with the program's own output, a Ctrl-C
# interrupt of a running inferior, fail-closed refusals, and the `tycho debug`
# dispatcher wrapper end to end. SKIPS loudly when gdb is absent (an external
# dependency like sqlite3/libpng, not a tool bug).
#
# It is the only lane that RUNS tycho-debug: scripts/tools_check.sh sweeps
# every .ty in the tree with `--emit-c`, so a syntax error already reddens
# step [9], but scripts/entrypoints.sh globs examples/*/ plus server/main.ty
# and never looks under tools/. Without this the debugger could stop breaking,
# stop stepping, or hang on Ctrl-C with every other gate green.
#
# ~6s, measured 2026-08-05. In `make ci` as step [3p/22].
# See tools/tycho-debug/run.sh.
debug-check: tychoc
	@sh tools/tycho-debug/run.sh

# q-check: the gate for tycho-q, the SQL-ish query tool in tools/tycho-q/.
# Same shape and same reasoning as ar-check above -- a batch program, so it gates
# with a golden rather than a daemon harness: build it, run 31 queries over
# fixtures the runner writes itself from literals, and compare the concatenated
# stdout against a recorded transcript.
#
# WHAT IT REDDENS FOR. tycho-q's only way to betray a caller is to return the
# WRONG ROWS and look like it worked, so the golden is a transcript rather than a
# row count: it carries the parsed s-expressions (precedence), the classification
# of every awkward cell, the `where` results, decimal arithmetic, the total order
# across null/bool/number/string, sort stability under BOTH directions, `limit`,
# and both readers. On top of it: `select *` must be byte-identical to the input
# file (a query that neither filters nor computes must not rewrite `007`, `-0` or
# a 26-digit integer); CSV and JSON must agree under `cmp`; and ten failure legs
# must exit non-zero with their reason on stderr and NOTHING on stdout.
#
# It is the only lane that RUNS tycho-q, exactly as ar-check is for tycho-ar, and
# for the same two reasons: scripts/tools_check.sh sweeps every .ty in the tree
# with `--emit-c`, so a syntax error already reddens step [9], but
# scripts/entrypoints.sh globs examples/*/ plus server/main.ty and never looks
# under tools/. So without this the evaluator could start dropping rows, ordering
# by string bytes, or truncating a decimal, with every other gate green.
#
# It also reddens for core:csv, core:json, core:decimal and core:sort: the
# transcript is a recorded assertion about what those four packages do, which is
# most of what makes it worth its 3.5s (measured 2026-08-01). In `make ci` as
# step [3f/13].
# See tools/tycho-q/run.sh.
q-check: tychoc
	@sh tools/tycho-q/run.sh

# vm-check: the gate for tycho-vm, the bytecode assembler/disassembler/
# interpreter in tools/tycho-vm/. Third of the same shape as ar-check and
# q-check above, and it exists for the same reason both of those do: step [9]
# tools-check `--emit-c`s every .ty in the tree, so a syntax error already
# reddens there, and [3b] entrypoints globs examples/*/ plus server/main.ty and
# never looks under tools/ -- so nothing RAN the VM.
#
# WHAT IT REDDENS FOR. A VM betrays its caller by computing the wrong answer
# quietly, so the golden carries the three programs' output (fib 0..89, gcd
# 6 21 1 6, sort 1 3 5 7 9) AND their disassembly listings, which is where an
# operand kind or a jump target moves. On top of the golden: each program
# assembled twice must be byte-identical (a non-deterministic writer), `asm` of
# `dis` must reproduce the .tyc byte for byte (a printer and a parser that
# disagree), and two traces of a program must be cmp-identical.
#
# The other half is the refusals: the seven runtime traps -- division by zero,
# stack underflow, stack overflow, jump out of range, bad slot index, call depth
# exceeded, bad const index -- must each exit non-zero NAMING THE PC with
# nothing on stdout, and four malformed .tasm inputs must each exit non-zero
# naming a LINE with nothing on stdout. Without the empty-stdout half a VM could
# print half a program's output and then die, which is worse than not running.
# The bad-const-index leg hex-patches an assembled .tyc, because the assembler
# range-checks that operand and there is no other route to the trap.
#
# ~2.3s, measured 2026-08-02. In `make ci` as step [3g/13].
# See tools/tycho-vm/run.sh.
vm-check: tychoc
	@sh tools/tycho-vm/run.sh

# scheme-check: the gate for tycho-scheme, the Scheme interpreter and its
# bytecode compiler in tools/tycho-scheme/. Fourth of the same shape as
# ar/q/vm-check above: nothing else RUNS a tool under tools/ (step [9]
# tools-check only --emit-c's them), so this is what executes the interpreter
# AND the compiler -- the compiled programs are run on tycho-vm and must match
# the interpreter byte-identically. The programs stay SHALLOW because their
# goldens are answers; the deep-recursion crash tests live in
# tests/recursion/run.sh.
#
# ~1s, measured 2026-08-03. In `make ci` as step [3h/14].
# See tools/tycho-scheme/run.sh.
scheme-check: tychoc
	@sh tools/tycho-scheme/run.sh

# kv-check: the gate for tycho-kv, the persistent B+ tree KV store in
# tools/tycho-kv/. Fifth of the same shape as the tool lanes: nothing else
# RUNS a tool under tools/ (step [9] tools-check only --emit-c's them), so
# this is what executes the store. The differential (B+ tree vs the naive
# map backend over the same command scripts, byte-identical) is the point.
#
# ~2s, measured 2026-08-03. In `make ci` as step [3i/15].
# See tools/tycho-kv/run.sh.
kv-check: tychoc
	@sh tools/tycho-kv/run.sh

# db-check: the gate for tycho-db, the relational database in tools/tycho-db/
# (sql/ parser, store/ heap file + equality index, plan/ planner, exec/
# operators, wal/ log, srv/ line protocol). Same shape and same reason
# as ar/q/vm/scheme/kv-check above: step [9] tools-check only --emit-c's a tool
# and [3b] entrypoints only compiles its main.ty, so until this lane existed
# NOTHING RAN the database -- it could have started returning the wrong rows,
# or lost every row it was given, with the whole sweep green.
#
# WHAT IT REDDENS FOR. A database betrays its caller two ways, and the lane is
# split accordingly. Wrong answers: the demo transcript is a golden carrying
# the ROWS, and the demo is run twice from a fresh store with both the stdout
# and the store FILE cmp'd. Lost data: one process writes and exits, a second
# opens the file and must read the rows back, a third writes to the REOPENED
# store and a fourth reads all four rows -- and those rows are literals in the
# runner, not a slice of the golden, so a re-record cannot bless a lost row.
#
# CRASH AND REPLAY is the third half, and the reason wal/ exists. A REAL
# kill -9 -- tycho-db's --crash-after=N hook runs kill(1) on its own pid --
# lands between the log write and the store write; a fresh process must then
# replay to every COMPLETED row and no partial one. Replay is asserted
# idempotent (a restored pre-checkpoint log must not double-apply) and a torn
# trailing record must be discarded rather than replayed, in both the
# zero-filled and the byte-flipped shape. Power-loss durability is now REQUESTED
# -- wal.ty calls io.sync (added 2026-08-12) on the log, the store and the parent
# directory -- but it is not asserted here and cannot be: a gate can kill a
# process, not cut the power. See the note in tools/tycho-db/run.sh.
#
# THE PLANNER is gated by a differential rather than a golden, because a plan
# layer that merely renamed the AST would pass a transcript diff. The index and
# the scan are run over identical rows and must return THE SAME rows, then must
# DIFFER in what they examined (1 against 6) -- correctness and "the index ran
# at all" are separate claims, and a probe silently falling back to a scan
# would satisfy the first alone. Constant folding is asserted by a WHERE 1 = 2
# examining zero rows of a table holding six.
#
# THE SERVER is driven over real sockets, never mocked. It binds port 0 and the
# gate reads the bound port out of the readiness banner, so no fixed port is
# claimed and no sleep is used to wait for it. A second connection must see the
# first one's writes (which is why the server checkpoints per session), a RAW
# SOCKET client asserts the wire format byte for byte so it cannot drift into
# "whatever our client sends", and two rude clients -- one hanging up
# mid-statement, one overrunning the 8 KiB line cap -- must each end their own
# session while a third client is still answered. Sessions are SERIALISED by
# design; see the concurrency note in srv/srv.ty for why an actor was not used.
#
# The other half is the refusals: all twenty-five named variants of store.StoreErr,
# exec.ExecErr, wal.WalErr, plan.PlanErr and srv.SrvErr must exit non-zero with THEIR OWN message,
# compared whole rather than by substring, with Corrupt and BadLog
# additionally leaving stdout empty. NotAPredicate is unreachable from SQL text
# (sql._predicate only builds Cmp and And, both of which exec._pred handles),
# so the runner copies the packages into its temp dir and probes the exec
# API directly; store.NoIndex is probed the same way, and asserting that
# REFUSAL is what rules out a probe() that quietly falls back to a scan. The variant list is EXTRACTED from the three enums and checked
# against what the runner covers, so a variant added later reddens here instead
# of arriving ungated.
#
# ~13.4s, measured 2026-08-12. In `make ci` as step [3p/22].
# See tools/tycho-db/run.sh.
db-check: tychoc
	@sh tools/tycho-db/run.sh

# flow-check: the gate for tycho-flow, the concurrent pipeline engine in
# tools/tycho-flow/. Same shape as the tool lanes above -- nothing else RUNS it
# -- but the subject is CONCURRENCY, so a golden diff is the weakest leg rather
# than the lane. The demo's transcript must be byte-identical over 8 default
# runs and at TYCHO_THREADS=1 and 2, because an answer that depends on the pool
# width depends on the machine. That proof would be vacuous if the pool never
# raced, so `--race 200` must find the pool draining out of source order on at
# least 190 of them, with 25 runs at one thread finding exactly 0 as the
# negative control. Backpressure is asserted against literals in the runner and
# not the golden: with a 4-slot ring and no receiver, exactly 4 markers can
# exist and the 5th cannot. All three stage.FlowErr variants must exit non-zero
# with their own whole message and an empty stdout, through a probe built
# against a COPY of stage/; the variant list is read out of the enum, so a new
# one cannot arrive ungated. And the whole demo plus 15 more pipelines run
# under TSan with a silent stderr -- a capture or ring bug is a data race, not
# a wrong answer, and every other leg here is blind to it (`make conc` is the
# precedent). The TSan leg SKIPS loudly where cc has no runtime.
#
# ~11.1s, measured 2026-08-12. In `make ci` as step [3q/23].
# See tools/tycho-flow/run.sh.
flow-check: tychoc
	@sh tools/tycho-flow/run.sh

# ed-check: the gate for tycho-ed, the terminal text editor in tools/tycho-ed/.
# Same shape as the tool lanes above -- nothing else RUNS it -- but the subject
# is UTF-8, which is the one thing a recorded transcript cannot see: a backspace
# that removes one BYTE of "é" leaves a lone 0xc3 that renders as plausible text
# and diffs clean against a golden recorded from the same broken build. So the
# byte and codepoint counts are asserted against literals IN THE RUNNER, where
# RECORD=1 cannot reach them: a backspace over a 2-byte codepoint must take the
# line from 13 bytes to 11 and 11 codepoints to 10, a forward delete of a 3-byte
# one from 19 to 16 and 13 to 12, and no dump anywhere may report INVALID UTF-8.
# The demo runs twice and both must be byte-identical. Six edits are undone to
# an empty buffer and redone back to a byte-identical dump -- cursor and journal
# depths included -- on a script the runner writes itself, because demo.ed only
# undoes past the bottom and never closes the loop. All seven buf.BufErr
# variants must exit non-zero with their own whole message and an empty stdout,
# through a probe built against a COPY of buf/; the --script driver reports them
# and exits 0 by design, so the probe is the only caller that can die by one.
# The variant list is read out of the enum, so a new one cannot arrive ungated.
# `--stress` is NOT run here: it is a timing measurement, written up in main.ty's
# header and in docs/thesis.md §4b, and a gate that asserted a timing is a coin
# toss.
#
# ~3.8s, measured 2026-08-12. In `make ci` as step [3r/24].
# See tools/tycho-ed/run.sh.
ed-check: tychoc
	@sh tools/tycho-ed/run.sh

# sheet-check: the gate for tycho-sheet, the spreadsheet engine in
# tools/tycho-sheet/. Same shape as the tool lanes above -- nothing else RUNS it
# -- and the same structural problem as ed-check, one subject over: here it is
# FLOAT TEXT, which a recorded transcript is blind to. If render() drops a digit,
# a golden re-recorded from that build agrees with it and `cmp` is green by
# construction. So the round trip is asserted IN THE RUNNER, where RECORD=1
# cannot reach it: 98411 generated values are rendered, parsed back and compared
# as DOUBLES, none may fail, and none may fall back to render()'s "#NUM!" arm.
# The four values that motivated cell/dtoa.ty -- 0.1+0.2, 2^53, DBL_MAX and the
# min subnormal -- are each asserted on their own, because the corpus count says
# 98411 round-trip but not which. 08dc88f8 made str(float) round-trip and so made
# most of cell/dtoa.ty redundant; the leg that used to assert str() was LOSSY is
# inverted rather than deleted, and now catches a revert. A cycle must be NAMED
# (F1 -> F2 -> F3 -> F1, and the self-reference G1 -> G1), not merely detected;
# 10000- and 100000-deep chains must evaluate exactly, and four different depth
# limits past them fail closed by name. All 14 CellErr and ParseErr variants bar
# one exit non-zero with their own whole message through a probe built on a COPY
# of cell/ and sheet/ -- the --script driver reports errors and exits 0 by
# design, so it cannot make that claim. Both variant lists are read out of the
# enums, so a new one cannot arrive ungated. The exception is CellErr.NoText,
# which bc51c069 made unreachable by teaching corelib subnormals; the runner
# asserts nothing reaches it AND that nothing in the source constructs it.
# Every run is bounded by timeout(1): a cycle detector that recursed forever is
# what half these legs exist to catch, and an unbounded gate would sit there
# until CI's own timeout killed it with no verdict.
#
# ~11s, measured 2026-08-12. In `make ci` as step [3s/25].
# See tools/tycho-sheet/run.sh.
sheet-check: tychoc
	@sh tools/tycho-sheet/run.sh

# sim-check: the gate for tycho-sim, the entity simulation in tools/tycho-sim/.
# It is the only lane that runs it, and the only program in the tree that uses
# `soa` and `subscript` as an API rather than as a fixture.
#
# Its subject is SWAP-REMOVE, which is the sibling of ed-check's UTF-8 and
# sheet-check's float text: a thing a recorded transcript cannot see. Despawn
# moves the last entity down over the hole and pops; forget to re-point the
# moved entity's slot and the POOL LENGTH IS STILL RIGHT -- every count, every
# field-wise sum over a dense walk and every "N live" line still read correctly,
# while exactly one id quietly starts addressing somebody else. A golden
# re-recorded from that build agrees with it.
#
# So the survivor SET is generated in the runner -- entity i is spawned with
# hp i*7+1 and the odd ones despawned, so the survivors are that expression over
# the even i -- and compared as a whole block, separately from the live count,
# which is computed from N. The two redden independently: a swap that forgets
# one component moves the set and leaves the count alone, and a pool that is
# never popped moves the count. A stale id must be refused BY THE GENERATION,
# with the reuse of the slot asserted too so the refusal is not just an empty
# slot. All three world.SimErr variants exit non-zero with their own whole
# message and an empty stdout, through a probe built against a COPY of world/;
# the driver reports a refusal and exits 0 by design, because a refusal is the
# observation. The variant list is read out of the enum.
#
# ~3.3s (3.32 / 3.33 / 3.34 s, measured 2026-08-13; two tychoc builds are most
# of it). In `make ci` as step [3t/26].
# See tools/tycho-sim/run.sh.
sim-check: tychoc
	@sh tools/tycho-sim/run.sh

# make-check: the gate for tycho-make, the build tool in tools/tycho-make/. It
# is the only lane that runs it, and the only lane that runs a SCHEDULER over a
# graph read at runtime.
#
# Its subject is a TOPOLOGICAL ORDER, which is the sibling of ed-check's UTF-8,
# sheet-check's float text and sim-check's swap-remove: a thing a recorded
# transcript cannot see. Drop an edge in the parser and the result is still an
# order -- the same nodes, each exactly once, in a sequence that looks entirely
# plausible -- and a golden re-recorded from that build agrees with it byte for
# byte. The only thing that moved is a constraint nobody printed.
#
# So the order is checked three ways RECORD=1 cannot reach. Against literals in
# the runner, over a demo rulefile built so DECLARATION order and ALPHABETICAL
# order disagree, which is what pins the tie-break rule rather than merely
# reproducing one answer. Against the edges the program itself printed, computed
# in the runner, which survives a change to the rulefile but is blind to a
# dropped edge by construction. And against a second rulefile exactly one edge
# apart from the first, which is the leg a parser that ignored dependencies
# entirely would fail. A cycle must be NAMED (`a -> c -> b -> a`, the self-edge
# `a -> a`) and named as the LOOP, not as the unfinished set -- one fixture puts
# two innocent nodes behind a cycle and the message may not mention them. Every
# run is bounded by timeout(1): a cycle detector that recursed forever is what
# half these legs exist to catch, and an unbounded gate would hang instead of
# reporting. All 8 graph.MakeErr variants exit non-zero with their own whole
# message and an empty stdout, and the variant list is read out of the enum.
#
# THE EXECUTOR half has the same problem one layer up: the build log is
# REASSEMBLED into topological order, so reading dependency order off the log is
# circular. Each recipe therefore appends its own name to a `trace` file, and
# specific PAIRS are asserted there -- three rules sit at one depth and race, so
# their order between themselves is deliberately not pinned. Staleness is asserted
# against literals in the runner: a no-op rebuild runs ZERO rules, changing one
# input reruns exactly its two dependents and no others, and moving a file's
# MTIME with its bytes intact reruns NOTHING and reports it `touched (content
# identical)` -- the one leg that can tell a content hash from a stat. The log is
# byte-identical over two runs at each of TYCHO_THREADS 1, 2 and 8, compared over
# a SEQUENCE (cold, no-op, one input changed) rather than a cold build, because a
# cold build's outcomes are all the same shape and a misfiled one is invisible in
# it. All 6 build.BuildErr variants are accounted for, the list read out of the
# enum; WorkLost guards the reassembly itself and is pinned to one construction
# site instead of a fixture.
#
# And one leg separates the WORK QUEUE from the wavefront it replaced, which no
# golden can: race.mk sits a 3-node instant chain beside three one-second
# sleepers at the same depth, and the chain's second node must START before the
# first sleeper finishes. Both designs build the same files and print the same
# reassembled log, so this is an ordering asserted in the runner. It reddens on
# the wavefront, measured 2026-08-13.
#
# ~4s (3.9 / 4.0 / 4.0 s, measured 2026-08-13, up from ~3s -- the work-queue leg
# adds one build whose recipes sleep). In `make ci` as step [3u/27]. See
# tools/tycho-make/run.sh.
make-check: tychoc
	@sh tools/tycho-make/run.sh

# diff-check: the gate for tycho-diff, the Myers line differ. Its subject is an
# EDIT SCRIPT, which a golden cannot judge -- two minimal scripts may differ, and
# GNU diff picks another tie-break on ~18% of random inputs while being just as
# right. So the golden pins the rendering and the lane asserts the two properties
# that define correctness: the script must rebuild BOTH files exactly, and its
# edit count must equal GNU's.
diff-check: tychoc
	@sh tools/tycho-diff/run.sh

# hash-check: the gate for tycho-hash, the parallel tree hasher. Its subject is a
# report that must not depend on the pool WIDTH, which a golden cannot see: a
# transcript recorded from a build whose workers raced is byte-identical to one
# that serialized. So the report is compared across 1/2/3/5/8 workers, the
# per-worker split is read back to prove the pool really shares (at width 1 the
# first worker must take ALL of them -- the negative control for --workers), and
# every hash is checked against sha256sum(1).
hash-check: tychoc
	@sh tools/tycho-hash/run.sh

# fold-check: the gate for tycho-fold, the UTF-8 text wrapper. Its subject is the
# byte-vs-codepoint distinction, which a golden is worst at judging: wrapping by
# BYTES still produces printable, plausible output in the wrong column, and a
# golden recorded from that build agrees with it. So the properties are computed
# and the byte mode is kept REACHABLE, so the lane can assert the two agree on
# ASCII and differ on non-ASCII.
fold-check: tychoc
	@sh tools/tycho-fold/run.sh

# snap-check: the gate for tycho-snap, the manifest-driven snapshot tool in
# tools/tycho-snap/. The subject is a ZIP ARCHIVE, so the numbers the program
# prints about it are its own CRCs of its own bytes -- python3's zipfile reads
# the archive here as an independent implementation, and the entry set and
# member ORDER are asserted against literals RECORD=1 cannot reach.
snap-check: tychoc
	@sh tools/tycho-snap/run.sh

# tally-check: the gate for tycho-tally, the SQLite ledger in tools/tycho-tally/.
# Its `--selftest` is 15 core:testing assertions, so the lane's load-bearing leg
# is the POSITIVE CONTROL: a copy of main.ty with one expected total changed must
# exit 1 and name the check. A framework that never fails would otherwise print
# `ok` forever. SKIPPED, exit 0, without sqlite3.
tally-check: tychoc
	@sh tools/tycho-tally/run.sh

# agg-check: the gate for tycho-agg, the group-and-count in tools/tycho-agg/.
# Its subject is USER-DEFINED GENERICS, and its load-bearing leg reads the
# emitted C for the mangled instantiations: a golden sees the counts, not whether
# the generics ran at all. No other run.sh greps a `pkg__fn__type` mangling.
agg-check: tychoc
	@sh tools/tycho-agg/run.sh

# tmpl-check: the gate for tycho-tmpl, the template renderer in tools/tycho-tmpl/.
# Its subject is `sink`, which had 18 uses before this program and all 18 in
# tests/. The load-bearing leg compiles FOUR probes that must each be REFUSED --
# a golden shows the rendered text, not that a consume rule still bites.
tmpl-check: tychoc
	@sh tools/tycho-tmpl/run.sh

# stat-check: the gate for tycho-stat, the statistics program in tools/tycho-stat/.
# Its subject is ARITHMETIC: variadics and `zero$(T)` were declared only in
# tests/ before this program, and a golden cannot see whether the numbers are
# right -- a fold that packed one argument still prints a plausible column. The
# load-bearing legs compute the expected count/sum/min/max/mean in the runner.
stat-check: tychoc
	@sh tools/tycho-stat/run.sh

# ledger-check: the gate for tycho-ledger, the per-account totals program in
# tools/tycho-ledger/. Its subject is the REFUSALS: a newtype is erased in
# lowering, so the transcript is identical whether the three domain types are
# distinct or the program used bare int/float/string. Five probes must each FAIL.
ledger-check: tychoc
	@sh tools/tycho-ledger/run.sh

# fh-check: the gate for tycho-fh, the handle-based file counter in
# tools/tycho-fh/. It is the ONLY program in the tree that declares a `handle`;
# all 10 prior declarations were fixtures. Its subject is "the destructor runs
# exactly once", which no transcript can show -- the C shim counts, and this
# lane reads the counters back plus six affine refusals.
fh-check: tychoc
	@sh tools/tycho-fh/run.sh

# grid-check: the gate for tycho-grid, the integer grid in tools/tycho-grid/.
# It is the SECOND consumer of `subscript`, `bounded[N]T` and `# deprecated:` --
# each had exactly one before. Its load-bearing legs are the ones a transcript
# cannot carry: the compile-time deprecation warnings (stderr, not stdout) and
# the five subscript declaration rules.
grid-check: tychoc
	@sh tools/tycho-grid/run.sh

# chess-check: the gate for tycho-chess, the perft + search engine in
# tools/tycho-chess/. Sixth of the same shape as the tool lanes: nothing else
# RUNS a tool under tools/ (step [9] tools-check only --emit-c's them), so
# this is what executes the engine. The ground-truth differential is the
# point: perft totals for the start position, Kiwipete and Position 3 against
# PUBLISHED values, plus ep/promotion/castling edge cases against the
# python-chess oracle, plus a divide transcript golden. The search section
# asserts determinism, TT-invariance (search == search-nott) and three exact
# tactical probes (a free queen, a hanging queen, scholar's mate).
#
# ~32s, measured 2026-08-04. In `make ci` as step [3j/16].
# See tools/tycho-chess/run.sh.
chess-check: tychoc
	@sh tools/tycho-chess/run.sh

# rsa-check: the gate for tycho-rsa, the pure-Tycho RSA implementation on
# core:bignum in tools/tycho-rsa/. Seventh of the same shape as the tool
# lanes: nothing else RUNS a tool under tools/, so this is what executes the
# arithmetic. The ground-truth differential is the point: the textbook RSA
# vector, modexp cross-checked against python pow() at 256/512/2048 bits,
# Miller-Rabin probes (incl. the Carmichael number 561), and a
# deterministic-seeded keygen whose invariants and round-trips are asserted
# and golden-locked, plus a 512-bit keygen round-trip through the CLI.
#
# ~4s, measured 2026-08-04. In `make ci` as step [3k/17].
# See tools/tycho-rsa/run.sh.
rsa-check: tychoc
	@sh tools/tycho-rsa/run.sh

# kvsrv-check: the gate for tycho-kvsrv, the concurrent HTTP key-value
# server in tools/tycho-kvsrv/. Eighth of the same shape as the tool lanes;
# a daemon cannot run to completion, so this follows the server/run.sh
# pattern -- start with --port 0, poll the stderr banner for the bound port,
# drive it with a raw-socket python client. Asserts the round-trips
# (PUT/GET/DELETE, 404 on a missing key), the 405/404 paths, keep-alive
# (two requests, one connection), and the concurrency probe (4 parallel
# clients PUT distinct keys, all 4 come back intact through the actor store).
#
# ~2s, measured 2026-08-04. In `make ci` as step [3l/18].
# See tools/tycho-kvsrv/run.sh.
kvsrv-check: tychoc
	@sh tools/tycho-kvsrv/run.sh

# sat-check: the gate for tycho-sat, the DPLL/CDCL SAT solver in
# tools/tycho-sat/. Ninth of the same shape as the tool lanes: nothing else
# RUNS a tool under tools/, so this is what executes the solver. The
# differential is a corpus, hermetic: PHP(2..9) must all be UNSAT (the
# theorem), and planted instances must be SAT with a model the runner's own
# checker verifies clause by clause. Determinism and the learning-vs-DPLL
# conflict counts are recorded in the golden.
#
# ~4s, measured 2026-08-04. In `make ci` as step [3m/19].
# See tools/tycho-sat/run.sh.
sat-check: tychoc
	@sh tools/tycho-sat/run.sh

# fetch: a CLI dogfood that composes core:http + json + sha256 + io + path,
# built by tychoc + ASan and run against a local file:// fixture (so the
# whole pipeline is deterministic + offline). Skips without libcurl.
# IN `make ci` since 2026-07-30 (step [3/13], beside site/raytrace/mandelbrot).
# It was standalone before that, and this comment said so as if it were a
# decision: nothing ran it, so its golden sat stale from 39d75be through an
# entire prior plan and five batches of this one before batch 5 re-recorded it.
# See examples/fetch/run.sh.
fetch: tychoc
	@sh examples/fetch/run.sh

# weblog / webserver: the last two examples/ dogfoods whose runner compared a
# golden that no lane ever ran. Both runners have compared their own `expected.out`
# with `diff -u` since they were written -- the hole was never a missing assertion,
# it was that `make ci` invoked neither, so `scripts/entrypoints.sh` (compile-only)
# was the whole of their coverage and the two goldens could rot exactly as
# examples/fetch's did before it joined step [3/13]. Same fix, same place: both are
# in `make ci` since 2026-08-02, inside step [3/13].
#
# Both are deterministic end to end, so the WHOLE of stdout is compared and nothing
# is excluded -- weblog parses a demo log embedded in its own source, and
# webserver's no-argument leg dispatches a fixed route list through the pure
# handler with no socket, hence no port. Measured 2026-08-02: three runs of one
# build, `cmp` silent across all three, for each. ~1.9s and ~2.1s (three runs each,
# same date), ~4s together, nearly all of it tychoc + cc.
#
# `webserver` is NOT `server-check`: that lane runs server/main.ty as a live daemon
# over a real socket. This one never binds. See examples/weblog/run.sh and
# examples/webserver/run.sh for what each golden does and does not assert.
weblog: tychoc
	@sh examples/weblog/run.sh

webserver: tychoc
	@sh examples/webserver/run.sh

# server: build tycho-httpd, the static web server in server/. A BUILD target
# only: it asserts nothing and is not in `make ci`. The gate is `server-check`
# below, which IS in `make ci` (step [3c/13]) since 2026-07-30. Until then this
# comment claimed the daemon was ungateable in principle -- "a long-running
# network daemon, not a fixture with a golden" -- and that was wrong twice over:
# the daemon shape is exactly what makes it gateable. It binds `--port 0` and
# prints the bound port in a startup banner (server/main.ty:610-614), so a runner
# gets readiness and the port from one line with no `sleep` and no fixed port to
# collide on, and a `trap` kills it on every exit path. What is unassertable is
# wall-clock (the concurrency and TCP_NODELAY numbers), not behaviour. Run this
# target and point a browser at it; see server/README.md.
server: tychoc
	@./tychoc server/main.ty -o tycho-httpd
	@echo "built ./tycho-httpd -- try: ./tycho-httpd --root server/www --port 8080"

# server-check: the gate `server` above is not. Starts tycho-httpd on --port 0,
# reads the bound port out of its stderr banner, talks HTTP to it over raw
# sockets (status codes, binary bodies, traversal, keep-alive, the abuse suite),
# checks the access log and the exit status, and kills it on every exit path.
# ~4s. Skips without python3. IN `make ci` since 2026-07-30, step [3c/13],
# immediately after `entrypoints`. See server/run.sh.
server-check: tychoc
	@sh server/run.sh

# site: a static-site generator dogfood composing eight corelib modules
# (io+path+json+csv+strings+sort+datetime+sha256) -- no FFI, no external deps, so
# it is deterministic and IS part of `make ci`. Built by tychoc + ASan against a
# fixture site, asserting the build report. See examples/site/run.sh.
site: tychoc
	@sh examples/site/run.sh

# raytrace: a small ray tracer -> QOI, stressing float-heavy Vec3 value semantics.
# Deterministic, so tychoc == ASan on the summary line. See
# examples/raytrace/run.sh. In `make ci`.
raytrace: tychoc
	@sh examples/raytrace/run.sh

# mandelbrot: a parallel Mandelbrot -- float compute inside a `parallel for`
# reduction. Deterministic, so tychoc == TSan == ASan on stdout. See
# examples/mandelbrot/run.sh. In `make ci`.
mandelbrot: tychoc
	@sh examples/mandelbrot/run.sh

# ILP32 conformance gate (the real int64 proof): rebuild every emitted fixture C
# under `gcc -m32` (a 32-bit-`long` / ILP32 data model) and golden-compare against
# the SAME tests/*.out recorded on 64-bit. They must match bit-for-bit because
# Tycho `int` is now the width-fixed `tycho_int` (int64_t): a value above 2^31
# would truncate under the old `long` lowering but must survive int64. This is the
# ONLY gate that proves the migration off LP64 (dev box has long==int64). Reuses
# tests/run.sh via TYCHO_NO_ASAN=1 (32-bit ASan runtime is absent under multilib;
# ASan stays covered by the 64-bit `make test`). Fails LOUDLY if -m32 is absent.
#
# WHY -msse2 -mfpmath=sse IS ON THE CC LINE. `gcc -m32` defaults to the x87 FPU,
# which evaluates double expressions in 80-bit registers and rounds to binary64
# only on store. docs/spec/03-types.md:55-56 says Tycho `float` IS an IEEE-754
# binary64 and that "its arithmetic, rounding, and special values follow
# IEEE-754" -- and docs/spec/appendix-f-impl-defined.md:44 makes only the textual form of
# NaN/inf implementation-defined, never the values. So x87 evaluation is not a
# permitted configuration for this language, and without these flags this lane
# was compiling Tycho floats at a precision the spec forbids. Measured: under
# `gcc -m32` alone, tests/float_lit_range.ty's binary64-exact `0.1 + 0.2 ==
# 0.30000000000000004` comes out FALSE; adding these two flags makes it true, and
# `-ffloat-store` does NOT (it rounds on store but not in the comparison).
# Every host that can already run this lane is x86-64, where SSE2 is baseline, so
# this narrows nothing. The lane's subject is integer width, not the FPU.
ilp32: tychoc
	@printf '#include <stdint.h>\n_Static_assert(sizeof(long)==4,"want ILP32 long");\nint main(void){int64_t x=5000000000LL;return x==5000000000LL?0:1;}\n' > build/.m32probe.c
	@gcc -m32 build/.m32probe.c -o build/.m32probe 2>build/.m32probe.log || { \
	  echo "ilp32: FATAL -- 'gcc -m32' cannot build a 32-bit int64 program." >&2; \
	  echo "ilp32: install gcc-multilib + lib32gcc-*-dev, or run on an ILP32 host. NOT skipping (RULE 4)." >&2; \
	  sed 's/^/ilp32:   /' build/.m32probe.log >&2; \
	  rm -f build/.m32probe.c build/.m32probe build/.m32probe.log; exit 1; }
	@rm -f build/.m32probe.c build/.m32probe build/.m32probe.log
	@echo "ilp32: -m32 toolchain OK (32-bit long, 64-bit int64_t verified)"
	@echo "ilp32: ASan lane SKIPPED for ilp32 (32-bit ASan runtime absent under multilib; 64-bit 'make test' covers ASan)"
	@CC="gcc -m32 -msse2 -mfpmath=sse" TYCHO_NO_ASAN=1 sh tests/run.sh

# FFI Stage 1 regression: extern fn (scalars + string) against a fixture C lib,
# ASan-clean, matched to a golden. See tests/ffi/run.sh.
ffi: tychoc
	@sh tests/ffi/run.sh

# Recursion-cap regression: deeply nested / long input must fail closed (clean
# nonzero exit) instead of overflowing the C stack (SIGSEGV).
# Covers parens, unary/operator chains, generic bodies, statement + type nesting
# (the COMPILER's own recursion), plus, since `docs/internals/plan-tycho-scheme-DONE.md` phase 1, the generated-code
# side: deep recursion in a PROGRAM (big frames, small frames, a spawned task's
# own stack) must fail closed via the runtime stack guard -- see the runner.
recursion: tychoc
	@sh tests/recursion/run.sh

# Soundness fuzzer: generate N random well-typed Tycho programs, compile each with
# tychoc twice -- native -O2 and ASan/UBSan -O1 -- and assert byte-identical output
# with no sanitizer fault (the native-vs-sanitizer differential of docs/thesis.md
# §3, on randomly generated programs instead of fixtures). N defaults to 200;
# failing programs are saved to fuzz/findings/. See fuzz/README.md.
N ?= 200
fuzz: tychoc
	@python3 fuzz/run.py $(N)

# Quick fuzz: a small differential+ASan sweep for the inner dev loop, so a
# compiler change can be smoke-tested in ~1-2 min instead of the full ~30 min.
# Full coverage stays `make fuzz` (N=200). Override the count: make fuzz-quick QN=120.
QN ?= 60
fuzz-quick: tychoc
	@python3 fuzz/run.py $(QN)

# Robustness lane: feed MALFORMED input to tychoc (built under ASan+UBSan) and
# assert it FAILS CLOSED -- never crashes, and any input it accepts must emit
# valid C. Wired into `make ci`.
fuzz-reject: tychoc
	@python3 fuzz/run_reject.py $(N)

# Leak lane: run the SOUNDNESS generator's valid programs (gen.py) under
# ASan+LeakSanitizer, SEQUENTIALLY, and assert nothing leaks at exit -- the one
# class the soundness lane (detect_leaks=0) can't see. Slowest lane (sequential
# ASan+LSan); wired into `make ci` capped at N=150 there.
# Run a deeper sweep directly: `make fuzz-leak N=500`.
fuzz-leak: tychoc
	@python3 fuzz/run_leak.py $(N)

# Wall-time regression guard: asserts tycho beats hand-written C on tree-alloc
# workloads (relative, machine-independent). Catches perf regressions that golden/
# fuzz/golden lanes can't (they check output, not speed -- see commit 4a5c64c). In CI.
bench-guard: tychoc
	@sh bench/guard.sh

# Local CI gate (NO GitHub Actions): build + test + corelib + fuzz + perf guard.
# The single "is the tree green" command. N defaults to 200 (override: make ci N=500 for a deeper sweep).
ci:
	@sh scripts/ci.sh $(N)

# Build the current-version tarball twice, smoke-test it, and require byte-identical output.
release-check: tychoc
	@set -eu; \
	  version="$$(./tychoc --version | awk '{print $$2}')"; \
	  os="$$(uname -s | tr '[:upper:]' '[:lower:]')"; \
	  archive="dist/tycho-v$$version-$$os-$$(uname -m).tar.gz"; \
	  first="$$(mktemp)"; trap 'rm -f "$$first"' EXIT HUP INT TERM; \
	  sh scripts/release.sh "v$$version"; cp "$$archive" "$$first"; \
	  sh scripts/release.sh "v$$version"; \
	  cmp -s "$$first" "$$archive" || { echo "release-check: archives differ" >&2; exit 1; }; \
	  echo "release-check: byte-identical archives"

# Activate the local git pre-push gate (.githooks/pre-push: make ci N=0 + fuzz-quick).
hooks:
	@git config core.hooksPath .githooks
	@echo "git hooks activated: core.hooksPath -> .githooks (pre-push runs make ci N=0 + fuzz-quick)"

# The `.c` arguments below are no longer left by `make tycho` / `make tychofmt` /
# `make tycho-lsp` -- the loops-cleanup plan made the plain build remove its own
# intermediate (src/tychoc.c:13707). They stay because `--emit-c -o <base>` still
# writes and KEEPS `<base>.c` (src/tychoc.c:13676-13678), which is how you debug the
# toolchain itself, and `clean` is where that leftover belongs. Same rationale as the
# matching .gitignore block; verified 2026-07-30, see the loops-cleanup plan.
clean:
	rm -f tychoc tycho tycho.c tychofmt tychofmt.c tycho-lsp tycho-lsp.c tycho-debug tycho-debug.c build/tycho_rt_embed.h
	rm -f tycho-httpd tycho-httpd.c
	rm -f examples/hello examples/hello.c examples/demo examples/demo.c
	rm -f examples/accumulate examples/accumulate.c
	rm -f examples/arrays examples/arrays.c
	rm -f examples/array_fns examples/array_fns.c
	rm -f examples/structs examples/structs.c
	rm -f examples/strings examples/strings.c
	rm -f examples/words examples/words.c
	rm -f examples/records examples/records.c
	rm -f examples/inout examples/inout.c
	rm -f examples/memo examples/memo.c
	rm -f examples/collect examples/collect.c
	rm -f examples/context examples/context.c
	-rmdir build 2>/dev/null || true
