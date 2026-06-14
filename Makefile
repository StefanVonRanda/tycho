# Hier compiler build.
#
#   make            -> build ./hierc (native, macOS/Linux host)
#   make demo       -> compile + run examples/hello.hi natively
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11

EMBED   := build/hier_rt_embed.h
RUNTIME := runtime/hier_rt.c

.PHONY: all tools demo test test-update conc bench bench-prongB bench-dbquery bench-conc bench-indexer bench-window bench-latency bench-gcscan bench-guard bootstrap fixpoint fuzz corelib ffi ci hooks clean

all: hierc

# Turn the runtime C source into a single C string literal the compiler
# embeds into every generated file. Each line is escaped and suffixed
# with \n so the emitted C is byte-for-byte the runtime source.
$(EMBED): $(RUNTIME) | build
	@awk 'BEGIN{print "static const char *HIER_RUNTIME ="} \
	     {gsub(/\\/,"\\\\"); gsub(/"/,"\\\""); printf "\"%s\\n\"\n",$$0} \
	     END{print ";"}' $(RUNTIME) > $(EMBED)

build:
	@mkdir -p build

hierc: src/hierc.c $(EMBED)
	$(CC) $(CFLAGS) -Ibuild src/hierc.c -o hierc

# The `hier` daily-driver CLI (run/build/check/watch) -- itself a Hier program,
# built with hierc + the FFI shell-out shim. Run as `./hier <cmd> <file.hi>`
# (set HIERC=./hierc to use the in-repo compiler). See tools/hier.hi.
hier: hierc tools/hier.hi tools/hier_shim.c
	./hierc tools/hier.hi --shim tools/hier_shim.c -o hier

# hierfmt -- the source formatter. STAGE 1: a lossless, comment-preserving lexer
# (round-trips every .hi file byte-for-byte); STAGE 2 will pretty-print. See
# tools/hierfmt.hi. `./hierfmt <file.hi>` (currently re-emits unchanged).
hierfmt: hierc tools/hierfmt.hi
	./hierc tools/hierfmt.hi -o hierfmt

# hier-lsp -- the language server (JSON-RPC over stdin/stdout, dogfooded in hier).
# STAGE 1: lifecycle handshake; stage 2 adds diagnostics. Any LSP client drives it.
hier-lsp: hierc tools/lsp.hi tools/lsp_shim.c
	./hierc tools/lsp.hi --shim tools/lsp_shim.c -o hier-lsp

# build the whole daily-driver toolchain (driver + formatter + language server)
tools: hier hierfmt hier-lsp

demo: hierc
	./hierc examples/hello.hi
	@echo "--- running examples/hello (type a name) ---"
	@./examples/hello

# Differential test suite: every examples/*.hi and tests/*.hi built both
# native -O2 and under -fsanitize=address,undefined, run on matching stdin,
# asserting exit 0, clean sanitizers, and byte-identical output. See
# tests/run.sh and docs/thesis.md §3.
test: hierc
	@sh tests/run.sh

# Concurrency suite (spawn/wait, parallel for, channels): hierc builds each
# positive fixture native + ASan/LSan + TSan against the goldens, the
# hierc-built hierc0 must reproduce the same outputs (parity differential),
# rejects must fail, aborts must die with their .err message. In `make ci`.
conc: hierc
	@sh tests/conc/run.sh

# Re-record the expected-output goldens (tests/*.out) from current output.
# Opt-in only: a normal `make test` never writes them, so a regression cannot
# silently rebake itself into the expected files. Review `git diff tests/`.
test-update: hierc
	@RECORD=1 sh tests/run.sh

# Performance guard: assert the thesis's optimizations still hold (peak RSS
# stays linear, the inout memo stays O(n)). See bench/run.sh.
bench: hierc
	@sh bench/run.sh

# Head-to-head memory benchmark: the same workloads in Hier / C / Rust / Go /
# Koka, peak RSS + wall time + output-identity, with a normalized scorecard.
# The empirical half of the thesis. See bench/prongB/run.sh and RESULTS.md.
bench-prongB: hierc
	@sh bench/prongB/run.sh

# Real-library head-to-head: the same SQLite workload in hier / C / Go (peak RSS
# + wall + identical checksum). Needs libsqlite3; skips absent toolchains. NOT in
# `make ci` (system dependency). See bench/dbquery/RESULTS.md.
bench-dbquery: hierc
	@sh bench/dbquery/run.sh

# Concurrency head-to-head (parallel reduce + channel pipeline) vs C/Go/Rust.
bench-conc: hierc
	@sh bench/conc/run.sh

# Parallel text indexer dogfood (channel fan-out -> worker maps -> inout merge)
# vs C/Go over an identical synthetic corpus. See bench/indexer/RESULTS.md.
bench-indexer: hierc
	@sh bench/indexer/run.sh

# Sliding-window eviction: the arena's weak point, mapped honestly (heap-record
# window loses ~14x; fixed-size ties). hier vs C vs Go. See bench/window/RESULTS.md.
bench-window: hierc
	@sh bench/window/run.sh

# Latency / GC-pause predictability: hier/C pause-free, Go's GC pause measured.
# See bench/latency/RESULTS.md.
bench-latency: hierc
	@sh bench/latency/run.sh

# Large held set: per-object overhead (hier most compact) + GC-scan cost (Go's
# memory-vs-CPU tradeoff under a GOGC sweep; hier/C never scan). bench/gcscan/RESULTS.md.
bench-gcscan: hierc
	@sh bench/gcscan/run.sh

# Self-hosting bootstrap: build hierc0 (the subset compiler written in Hier)
# and validate it on its fixtures. See compiler/ and docs/bootstrap.md.
bootstrap: hierc
	@sh compiler/run.sh

# corelib: the standard library (packages under corelib/, imported as `core:<name>`,
# resolved via HIER_CORELIB). Each corelib/test/<name> must compile + run identically
# through the C compiler and the self-hosted hierc0. See corelib/run.sh.
corelib: hierc
	@sh corelib/run.sh

# FFI Stage 1 regression: extern fn (scalars + string) against a fixture C lib,
# through BOTH compilers, ASan-clean, matched to a golden. See tests/ffi/run.sh.
ffi: hierc
	@sh tests/ffi/run.sh

# Stage 4 self-host fixpoint: A=hierc·hierc0.hi, B=A·hierc0.hi, C=B·hierc0.hi;
# assert B==C (byte-identical self-emission) and B matches the C compiler.
fixpoint: hierc
	@sh compiler/fixpoint.sh

# Soundness fuzzer: generate N random well-typed Hier programs, compile each
# with hierc (reference, native) and hierc0 (native + ASan/UBSan), and assert
# byte-identical output with no sanitizer fault. N defaults to 500; failing
# programs are saved to fuzz/findings/. See fuzz/README.md.
N ?= 500
fuzz: hierc
	@python3 fuzz/run.py $(N)

# Robustness lane: feed MALFORMED input to BOTH compilers (built under
# ASan+UBSan) and assert each FAILS CLOSED -- never crashes, and any input it
# accepts must emit valid C. Wired into `make ci` (scripts/ci.sh, step 8/9).
fuzz-reject: hierc
	@python3 fuzz/run_reject.py $(N)

# Leak lane: run the SOUNDNESS generator's valid programs (gen.py) under
# ASan+LeakSanitizer, SEQUENTIALLY, and assert nothing leaks at exit -- the one
# class the differential lane (detect_leaks=0) can't see. Slowest lane (sequential
# ASan+LSan); wired into `make ci` (scripts/ci.sh step 9/10) capped at N=150 there.
# Run a deeper sweep directly: `make fuzz-leak N=500`.
fuzz-leak: hierc
	@python3 fuzz/run_leak.py $(N)

# Wall-time regression guard: asserts hier beats hand-written C on tree-alloc
# workloads (relative, machine-independent). Catches perf regressions that golden/
# fuzz/fixpoint can't (they check output, not speed -- see commit 6ff7aa1). In CI.
bench-guard: hierc
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
	rm -f hierc hier hier.c hierfmt hierfmt.c hier-lsp hier-lsp.c build/hier_rt_embed.h
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
