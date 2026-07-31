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

.PHONY: all tools tools-check demo test test-fast prunner test-update conc rtparity bench bench-prongB bench-dbquery bench-conc bench-indexer bench-window bench-latency bench-gcscan bench-guard bench-site fuzz fuzz-quick fuzz-reject fuzz-leak corelib corelib-examples shim-check fetch site raytrace mandelbrot ffi recursion entrypoints spec-check docs-fences check-links server server-check wiki ci hooks ilp32 asan-self editors-check clean

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

# build the whole daily-driver toolchain (driver + formatter + language server)
tools: tycho tychofmt tycho-lsp

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

# Differential test suite: every examples/*.ty and tests/*.ty built both
# native -O2 and under -fsanitize=address,undefined, run on matching stdin,
# asserting exit 0, clean sanitizers, and byte-identical output. See
# tests/run.sh and docs/thesis.md §3.
test: tychoc
	@sh tests/run.sh

# The same 560 fixtures over a bounded worker pool, in Tycho: tools/prunner/main.ty.
# 7 m 54 s -> 1 m 02 s on a 16-core box, and its report is byte-identical to
# `tests/run.sh`'s over the whole corpus (docs/internals/plan-prunner-DONE.md phase 2).
#
# ADVISORY, NOT AUTHORITATIVE, and `test` above is deliberately still the shell
# script. prunner is compiled by the compiler it tests, so a tychoc regression
# can land inside its judge and turn every verdict green at once; run.sh scores
# with cmp/grep/test, which nothing in this repo can break. Use `test-fast` while
# iterating, `test` to believe the answer. When they disagree, run.sh is right.
# Width is ncpu(); TYCHO_THREADS=N narrows it (there is no -j -- docs/internals/plan-prunner-DONE.md phase 3).
build/prunner: tools/prunner/main.ty tychoc | build
	@./tychoc tools/prunner/main.ty -o build/prunner

prunner: build/prunner

test-fast: build/prunner
	@./build/prunner

# The COMPILER's own memory safety. Every other sanitizer lane here (including
# `test` above) sanitizes the C tychoc EMITS; nothing built src/tychoc.c itself
# with -fsanitize, so tychoc's own execution was unmeasured by every gate -- which
# is how the parse_type_inner stack-buffer-overflow of docs/internals/plan-front-door-DONE.md phase 37 survived a
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
# cc). In `make ci` as step [2d/13] since 2026-07-30 (docs/internals/plan-loops-cleanup-DONE.md phase 58) -- a
# tests/ lane, so a sub-lane of step 2 rather than a new number. It paid for
# itself on the way in: it was the only gate that saw phase 53 remove the
# "tycho: range step is zero" trap from the emitted C.
rtparity: tychoc
	@python3 tests/rtparity/run.py

# Re-record the expected-output goldens (tests/*.out) from current output.
# Opt-in only: a normal `make test` never writes them, so a regression cannot
# silently rebake itself into the expected files. Review `git diff tests/`.
test-update: tychoc
	@RECORD=1 sh tests/run.sh

# Performance guard: assert the thesis's optimizations still hold (peak RSS
# stays linear, the inout memo stays O(n)). See bench/run.sh.
bench: tychoc
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
	@CC="gcc -m32" TYCHO_NO_ASAN=1 sh tests/run.sh

# FFI Stage 1 regression: extern fn (scalars + string) against a fixture C lib,
# ASan-clean, matched to a golden. See tests/ffi/run.sh.
ffi: tychoc
	@sh tests/ffi/run.sh

# Recursion-cap regression: deeply nested / long input must fail closed (clean
# nonzero exit) instead of overflowing the C stack (SIGSEGV).
# Covers parens, unary/operator chains, generic bodies, statement + type nesting.
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
# fuzz/golden lanes can't (they check output, not speed -- see commit 6ff7aa1). In CI.
bench-guard: tychoc
	@sh bench/guard.sh

# Local CI gate (NO GitHub Actions): build + test + corelib + fuzz + perf guard.
# The single "is the tree green" command. N defaults to 200 (override: make ci N=500 for a deeper sweep).
ci:
	@sh scripts/ci.sh $(N)

# Activate the local git pre-push gate (.githooks/pre-push: make ci N=0 + fuzz-quick).
hooks:
	@git config core.hooksPath .githooks
	@echo "git hooks activated: core.hooksPath -> .githooks (pre-push runs make ci N=0 + fuzz-quick)"

# The `.c` arguments below are no longer left by `make tycho` / `make tychofmt` /
# `make tycho-lsp` -- docs/internals/plan-loops-cleanup-DONE.md phase 52 made the plain build remove its own
# intermediate (src/tychoc.c:12771). They stay because `--emit-c -o <base>` still
# writes and KEEPS `<base>.c` (src/tychoc.c:12740-12742), which is how you debug the
# toolchain itself, and `clean` is where that leftover belongs. Same rationale as the
# matching .gitignore block; verified 2026-07-30, see docs/internals/plan-loops-cleanup-DONE.md phase 57.
clean:
	rm -f tychoc tycho tycho.c tychofmt tychofmt.c tycho-lsp tycho-lsp.c build/tycho_rt_embed.h
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
