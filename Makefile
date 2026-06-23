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

.PHONY: all tools tools-check demo test test-update conc bench bench-prongB bench-dbquery bench-conc bench-indexer bench-window bench-latency bench-gcscan bench-guard bench-site bootstrap fixpoint fuzz fuzz-reject fuzz-leak typeparity parforparity eqparity unaryparity corelib corelib-examples fetch site ffi ci hooks clean

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

# Concurrency suite (spawn/wait, parallel for, channels): tychoc builds each
# positive fixture native + ASan/LSan + TSan against the goldens, the
# tychoc-built tychoc0 must reproduce the same outputs (parity differential),
# rejects must fail, aborts must die with their .err message. In `make ci`.
conc: tychoc
	@sh tests/conc/run.sh

# Re-record the expected-output goldens (tests/*.out) from current output.
# Opt-in only: a normal `make test` never writes them, so a regression cannot
# silently rebake itself into the expected files. Review `git diff tests/`.
test-update: tychoc
	@RECORD=1 sh tests/run.sh

# Performance guard: assert the thesis's optimizations still hold (peak RSS
# stays linear, the mut memo stays O(n)). See bench/run.sh.
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

# Parallel text indexer dogfood (channel fan-out -> worker maps -> mut merge)
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

# Self-hosting bootstrap: build tychoc0 (the subset compiler written in Tycho)
# and validate it on its fixtures. See compiler/.
bootstrap: tychoc
	@sh compiler/run.sh

# corelib: the standard library (packages under corelib/, imported as `core:<name>`,
# resolved via TYCHO_CORELIB). Each corelib/test/<name> must compile + run identically
# through the C compiler and the self-hosted tychoc0. See corelib/run.sh.
corelib: tychoc
	@sh corelib/run.sh

# corelib examples: a small, readable program per core module (usage as
# documentation, not assertions like corelib/test/), validated 3-way + golden
# like the tests, with the same deps-skip. See examples/corelib/run.sh.
corelib-examples: tychoc
	@sh examples/corelib/run.sh

# fetch: a CLI dogfood that composes core:http + json + sha256 + io + path,
# built by both compilers + ASan and run against a local file:// fixture (so the
# whole pipeline is deterministic + offline). Skips without libcurl. Standalone
# (not in `make ci`, like examples/sqlite); the http module is covered in ci via
# corelib-examples. See examples/fetch/run.sh.
fetch: tychoc
	@sh examples/fetch/run.sh

# site: a static-site generator dogfood composing eight corelib modules
# (io+path+json+csv+strings+sort+datetime+sha256) -- no FFI, no external deps, so
# it is deterministic and IS part of `make ci`. Built by all three compilers +
# ASan against a fixture site, asserting the build report. See examples/site/run.sh.
site: tychoc
	@sh examples/site/run.sh

# FFI Stage 1 regression: extern fn (scalars + string) against a fixture C lib,
# through BOTH compilers, ASan-clean, matched to a golden. See tests/ffi/run.sh.
ffi: tychoc
	@sh tests/ffi/run.sh

# Stage 4 self-host fixpoint: A=tychoc·tychoc0.ty, B=A·tychoc0.ty, C=B·tychoc0.ty;
# assert B==C (byte-identical self-emission) and B matches the C compiler.
fixpoint: tychoc
	@sh compiler/fixpoint.sh

# Soundness fuzzer: generate N random well-typed Tycho programs, compile each
# with tychoc (reference, native) and tychoc0 (native + ASan/UBSan), and assert
# byte-identical output with no sanitizer fault. N defaults to 500; failing
# programs are saved to fuzz/findings/. See fuzz/README.md.
N ?= 500
fuzz: tychoc
	@python3 fuzz/run.py $(N)

# Robustness lane: feed MALFORMED input to BOTH compilers (built under
# ASan+UBSan) and assert each FAILS CLOSED -- never crashes, and any input it
# accepts must emit valid C. Wired into `make ci` (scripts/ci.sh, step 8/9).
fuzz-reject: tychoc
	@python3 fuzz/run_reject.py $(N)

# Leak lane: run the SOUNDNESS generator's valid programs (gen.py) under
# ASan+LeakSanitizer, SEQUENTIALLY, and assert nothing leaks at exit -- the one
# class the differential lane (detect_leaks=0) can't see. Slowest lane (sequential
# ASan+LSan); wired into `make ci` (scripts/ci.sh step 9/10) capped at N=150 there.
# Run a deeper sweep directly: `make fuzz-leak N=500`.
fuzz-leak: tychoc
	@python3 fuzz/run_leak.py $(N)

# Type-parity lane: assert tychoc and tychoc0 agree on accept/reject for the
# EXHAUSTIVE scalar binary-operator matrix (every type x literal/var x operator).
# A TYPE-boundary accept/reject divergence is a bug -- unlike the tolerated
# grammar-boundary divergence in fuzz-reject. Deterministic, no seeds. In `make ci`.
typeparity: tychoc
	@python3 fuzz/run_typeparity.py

# parallel-for GATE accept/reject parity: tychoc and tychoc0 must agree on whether
# a `parallel for` body is legal (no early exit / captured-var mutation / non-
# reduction outer update / non-int range). The fixpoint differential is output-
# only and the reject harness gates tychoc alone, so a tychoc0 fail-open here is
# otherwise invisible -- this closed 12 of them. Deterministic, no seeds. In CI.
parforparity: tychoc
	@python3 fuzz/run_parforparity.py

# composite/newtype ==,!= accept/reject parity: tychoc and tychoc0 must agree on
# whether equality over arrays/options/structs/tuples/maps/newtypes type-checks.
# tychoc0's structural-eq codegen keyed off the LEFT operand only, so every
# composite mismatch (`[int] == [string]`, `xs == 7`, ...) over-accepted -- 330
# of these, invisible to the OUTPUT-only fixpoint differential. Deterministic,
# no seeds (4 newtype-erasure pairs skipped by design). In CI.
eqparity: tychoc
	@python3 fuzz/run_eqparity.py

# unary-operator accept/reject parity: tychoc and tychoc0 must agree on `-x`,
# `~x`, `not x`. tychoc0 used to desugar `-x`/`~x` into binary arithmetic at
# parse, so the permissive arithmetic rules over-accepted `~float`, `~char`,
# `-char` (4 fail-opens) -- tychoc's unary rules are stricter. Deterministic,
# no seeds (1 newtype-erasure pair skipped). In CI.
unaryparity: tychoc
	@python3 fuzz/run_unaryparity.py

# Wall-time regression guard: asserts tycho beats hand-written C on tree-alloc
# workloads (relative, machine-independent). Catches perf regressions that golden/
# fuzz/fixpoint can't (they check output, not speed -- see commit 6ff7aa1). In CI.
bench-guard: tychoc
	@sh bench/guard.sh

# Local CI gate (NO GitHub Actions): build + test + fixpoint + fuzz + perf guard.
# The single "is the tree green" command. N defaults to 500 (override: make ci N=200).
ci:
	@sh scripts/ci.sh $(N)

# Activate the local git pre-push gate (.githooks/pre-push: test + fixpoint).
hooks:
	@git config core.hooksPath .githooks
	@echo "git hooks activated: core.hooksPath -> .githooks (pre-push runs test + fixpoint)"

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
