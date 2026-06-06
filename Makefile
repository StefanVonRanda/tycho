# Hier compiler build.
#
#   make            -> build ./hierc (native, macOS/Linux host)
#   make demo       -> compile + run examples/hello.hi natively
#   make image      -> build the alpine static-build podman image
#   make static HI=examples/hello.hi  -> produce a static linux binary
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11

EMBED   := build/hier_rt_embed.h
RUNTIME := runtime/hier_rt.c

.PHONY: all demo test test-update bench bench-prongB bootstrap fixpoint fuzz corelib ffi ci hooks image static clean

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

# Local CI gate (NO GitHub Actions): build + test + fixpoint + fuzz. The single
# "is the tree green" command. N defaults to 500 (override: make ci N=200).
ci:
	@sh scripts/ci.sh $(N)

# Activate the local git pre-push gate (.githooks/pre-push: test + fixpoint).
hooks:
	@git config core.hooksPath .githooks
	@echo "git hooks activated: core.hooksPath -> .githooks (pre-push runs test + fixpoint)"

image:
	podman build -t hier-build -f podman/Dockerfile .

# Produce a fully static linux binary from a .hi file using the alpine
# image. HI defaults to the hello example.
HI ?= examples/hello.hi
static: hierc image
	./hierc $(HI) --emit-c
	podman run --rm -v "$(CURDIR)":/work -w /work hier-build \
	    sh -c 'gcc -static -O2 -o $(basename $(HI)) $(basename $(HI)).c -lm && echo "static build ok:" && file $(basename $(HI))'

clean:
	rm -f hierc build/hier_rt_embed.h
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
	rm -f examples/strings examples/strings.c
	rm -f examples/words examples/words.c
	rm -f examples/records examples/records.c
	-rmdir build 2>/dev/null || true
