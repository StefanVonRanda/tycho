
CC      ?= cc
# -fwrapv: signed integer overflow wraps, never C UB. The compiler's own
# arithmetic honours the same contract it gives generated programs.
CFLAGS  ?= -O2 -fwrapv -Wall -Wextra -std=c11

# Which compiler the bench lanes build and measure. Override to point the suite
# at the second compiler: make bench-guard TYCHOC=./tychoc1
TYCHOC  ?= ./tychoc

EMBED   := build/tycho_rt_embed.h
RUNTIME := runtime/tycho_rt.c

.PHONY: corpus-check packed-check vector-check align-probe status status-net status-check parse-check tychoc1-check script-check friction-check surface-check net-poll-check version-check all tools tools-check demo test test-fast prunner test-update conc rtparity bench bench-prongB bench-dbquery bench-conc bench-indexer bench-window bench-latency bench-gcscan bench-guard bench-site fuzz fuzz-quick fuzz-reject fuzz-leak corelib corelib-examples shim-check shim-warn source-bytes goldens-check tls-verify http-verify handle-guard format-diff math-diff traversal-check ar-check build-check debug-check q-check vm-check scheme-check kv-check db-check flow-check ed-check sheet-check sim-check make-check snap-check tally-check agg-check tmpl-check stat-check ledger-check fh-check grid-check chess-check kvsrv-check sat-check locale-check fetch weblog webserver site raytrace mandelbrot ffi recursion entrypoints spec-check spec-fast docs-fences check-links server server-check wiki ci release-check release-content hooks ilp32 asan-self editors-check clean

# tychoc1, the self-hosted compiler, is what `make` produces and what ships.
# It still depends on tychoc: src/tychoc.c is the bootstrap stage that builds it.
all: tychoc1

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

# The second compiler, written in Tycho and built by the C bootstrap. It reads
# runtime/tycho_rt.c at emit time rather than embedding it, so it must be run
# from the repo root or given --runtime.
TYCHOC1_SRC := compiler/main.ty $(wildcard compiler/*/*.ty)

# TWO-STAGE. Stage 1 is built by ./tychoc; stage 2 is built by stage 1, so the
# shipped compiler carries its OWN elisions. Stage 1 lives in the repo root, not
# under build/, because the corelib is located relative to the binary.
# TYCHOC1_CFLAGS picks the COMPILER BINARY's own cc flags, the way -O2 above
# picks ./tychoc's. It is not the default for user programs: driver.ty stays
# -O3, because inline-unit-growth costs bench/transient 3-5%. On this one
# translation unit it is worth 1051.3e6 -> 864.2e6 Ir.
# -static-pie, not -static: a plain static link leaves the .eh_frame table
# unsorted, so the first unwind runs classify_object_over_fdes over the whole
# thing -- 201,604 Ir on EVERY compile regardless of input size (48% of a
# two-line program, measured identical on tiny.ty and examples/invindex.ty).
TYCHOC1_CFLAGS ?= --param inline-unit-growth=150 -static-pie -flto
# `make clean && make` is the bootstrap check: clean removes tychoc1, so this
# line runs for real. 8c156d38 swapped it to ./tychoc1 and only a fresh clone saw it.
tychoc1: tychoc $(TYCHOC1_SRC)
	./tychoc compiler/main.ty -o tychoc1-stage1
	TYCHO_CFLAGS="$(TYCHOC1_CFLAGS)" ./tychoc1-stage1 compiler/main.ty -o tychoc1
	@rm -f tychoc1-stage1

tycho: tychoc1 tools/tycho.ty tools/tycho_shim.c
	./tychoc1 tools/tycho.ty --shim tools/tycho_shim.c -o tycho

tychofmt: tychoc1 tools/tychofmt.ty
	./tychoc1 tools/tychofmt.ty -o tychofmt

tycho-lsp: tychoc1 tools/lsp.ty tools/lsp_shim.c
	./tychoc1 tools/lsp.ty --shim tools/lsp_shim.c -o tycho-lsp

tycho-debug: tychoc1 tools/tycho-debug/main.ty tools/tycho-debug/debug_shim.c
	./tychoc1 tools/tycho-debug/main.ty --shim tools/tycho-debug/debug_shim.c -o tycho-debug

tools: tycho tychofmt tycho-lsp tycho-debug

tools-check: tychoc1
	@sh scripts/tools_check.sh

editors-check:
	@sh scripts/editors_check.sh

entrypoints: tychoc1
	@sh scripts/entrypoints.sh

spec-check:
	@sh scripts/spec_check.sh

# The text half of spec-check -- Appendix A vs the chapters, and Appendix E's
# fixture citations. ~0.03s against ~20s for the whole gate, which is what makes
# it affordable in .githooks/pre-push. See scripts/spec_check.sh.
spec-fast:
	@sh scripts/spec_check.sh --fast

docs-fences: tychoc1
	@python3 scripts/docs_fences.py

check-links:
	@sh scripts/check_links.sh
	@python3 scripts/check_citations.py --selfcheck
	@python3 scripts/check_citations.py
# reanchor_citations.py is the tool that MAINTAINS the refs check_citations.py
# scores, so its selfcheck belongs in the same lane. It ran only when somebody
# typed it; script-check merely parses the file.
	@python3 scripts/reanchor_citations.py --selfcheck

WIKI_DIR ?= .wiki
WIKI_REMOTE ?= https://github.com/StefanVonRanda/tycho.wiki.git
wiki:
	@if [ -d $(WIKI_DIR)/.git ]; then git -C $(WIKI_DIR) pull --ff-only; \
	else git clone $(WIKI_REMOTE) $(WIKI_DIR); fi
	@python3 scripts/sync-wiki.py $(WIKI_DIR)
	@echo "Review $(WIKI_DIR)/, then: git -C $(WIKI_DIR) add -A && git -C $(WIKI_DIR) commit -m ... && git -C $(WIKI_DIR) push"

demo: tychoc1
	./tychoc examples/hello.ty
	@echo "--- running examples/hello (type a name) ---"
	@./examples/hello

wine-smoke:
	@sh scripts/wine_smoke.sh

wine-test:
	@sh scripts/wine_test.sh

wine-corelib:
	@sh scripts/wine_corelib.sh

wine-ubsan:
	@sh scripts/wine_ubsan.sh

wine-tools:
	@sh scripts/wine_tools.sh

wine-ffi:
	@sh scripts/wine_ffi.sh

test: tychoc1
	@sh tests/run.sh

build/prunner: tools/prunner/main.ty tychoc | build
	@./tychoc tools/prunner/main.ty -o build/prunner

prunner: build/prunner

test-fast: build/prunner
	@./build/prunner

asan-self: $(EMBED)
	@sh scripts/asan_self.sh

conc: tychoc1
	@sh tests/conc/run.sh

rtparity: tychoc1
	@python3 tests/rtparity/run.py

locale-check: tychoc1
	@sh scripts/locale_check.sh

test-update: tychoc1
	@RECORD=1 sh tests/run.sh

bench: $(TYCHOC) entrypoints
	@TYCHOC=$(TYCHOC) sh bench/run.sh

bench-prongB: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/prongB/run.sh

bench-dbquery: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/dbquery/run.sh

bench-conc: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/conc/run.sh

bench-indexer: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/indexer/run.sh

bench-site: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/site/run.sh

bench-window: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/window/run.sh

bench-latency: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/latency/run.sh

bench-gcscan: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/gcscan/run.sh

corelib: tychoc1
	@sh corelib/run.sh

corelib-examples: tychoc1
	@sh examples/corelib/run.sh

# `packed struct` layout, in BOTH compilers. No golden can see this feature:
# tests/packed_struct.ty prints the same lines whether the attribute reached the
# emitted C or not, and only sizeof moves. Needs tychoc and tychoc1 built.
packed-check: tychoc tychoc1
	@sh scripts/packed_check.sh

vector-check: tychoc tychoc1
	@sh scripts/vector_check.sh

align-probe: tychoc tychoc1
	@sh scripts/align_probe.sh

shim-check:
	@sh scripts/shim_check.sh

# The shims' WARNINGS, against a locked baseline -- shim-check compiles them
# without -Wall and so cannot see one.
shim-warn:
	@sh scripts/shim_warn.sh

# The BYTES of a source file -- a CRLF line ending and a NUL byte -- which no
# checked-in fixture can carry (git normalises one, the other is a binary blob).
source-bytes: tychoc tychoc1
	@sh scripts/source_bytes.sh

goldens-check:
	@python3 scripts/check_goldens.py

# The sub-second predictor of parse-check's opening counts. No compiler, so a
# commit that adds or deletes a .ty can afford it; parse-check runs it too.
corpus-check:
	@python3 scripts/check_corpus_counts.py
	@python3 scripts/check_corpus_counts.py --selfcheck

parse-check: tychoc1
	@sh compiler/run.sh

tychoc1-check: tychoc1
	@sh scripts/tychoc1_check.sh $(ONLY)

tls-verify:
	@sh scripts/tls_verify.sh

handle-guard:
	@sh scripts/handle_guard.sh

http-verify:
	@sh scripts/http_verify.sh

format-diff:
	@sh scripts/format_diff.sh

math-diff:
	@sh scripts/math_diff.sh

net-poll-check:
	@sh scripts/net_poll_check.sh

traversal-check:
	@sh scripts/traversal_depth.sh

ar-check: tychoc1
	@sh tools/tycho-ar/run.sh

image-ceiling: tychoc1
	@sh scripts/image_ceiling.sh

# The core:zip sibling of image-ceiling. tycho-ar's bomb leg covers gzip via
# compress.decompress; zip entries are RAW deflate, a different function.
zip-ceiling: tychoc1
	@sh scripts/zip_ceiling.sh

build-check: tychoc1
	@sh tools/tycho-build/run.sh

debug-check: tychoc1
	@sh tools/tycho-debug/run.sh

q-check: tychoc1
	@sh tools/tycho-q/run.sh

vm-check: tychoc1
	@sh tools/tycho-vm/run.sh

scheme-check: tychoc1
	@sh tools/tycho-scheme/run.sh

kv-check: tychoc1
	@sh tools/tycho-kv/run.sh

db-check: tychoc1
	@sh tools/tycho-db/run.sh

flow-check: tychoc1
	@sh tools/tycho-flow/run.sh

ed-check: tychoc1
	@sh tools/tycho-ed/run.sh

sheet-check: tychoc1
	@sh tools/tycho-sheet/run.sh

sim-check: tychoc1
	@sh tools/tycho-sim/run.sh

make-check: tychoc1
	@sh tools/tycho-make/run.sh

diff-check: tychoc1
	@sh tools/tycho-diff/run.sh

hash-check: tychoc1
	@sh tools/tycho-hash/run.sh

fold-check: tychoc1
	@sh tools/tycho-fold/run.sh

snap-check: tychoc1
	@sh tools/tycho-snap/run.sh

tally-check: tychoc1
	@sh tools/tycho-tally/run.sh

agg-check: tychoc1
	@sh tools/tycho-agg/run.sh

tmpl-check: tychoc1
	@sh tools/tycho-tmpl/run.sh

stat-check: tychoc1
	@sh tools/tycho-stat/run.sh

ledger-check: tychoc1
	@sh tools/tycho-ledger/run.sh

fh-check: tychoc1
	@sh tools/tycho-fh/run.sh

grid-check: tychoc1
	@sh tools/tycho-grid/run.sh

chess-check: tychoc1
	@sh tools/tycho-chess/run.sh

kvsrv-check: tychoc1
	@sh tools/tycho-kvsrv/run.sh

sat-check: tychoc1
	@sh tools/tycho-sat/run.sh

fetch: tychoc1
	@sh examples/fetch/run.sh

weblog: tychoc1
	@sh examples/weblog/run.sh

webserver: tychoc1
	@sh examples/webserver/run.sh

server: tychoc1
	@./tychoc server/main.ty -o tycho-httpd
	@echo "built ./tycho-httpd -- try: ./tycho-httpd --root server/www --port 8080"

server-check: tychoc1
	@sh server/run.sh

site: tychoc1
	@sh examples/site/run.sh

raytrace: tychoc1
	@sh examples/raytrace/run.sh

mandelbrot: tychoc1
	@sh examples/mandelbrot/run.sh

# Rebuild every emitted fixture C under a 32-bit-long data model and compare
# against the same goldens: Tycho int is int64_t, so a value above 2^31 must
# survive. -msse2 -mfpmath=sse because gcc -m32 defaults to the x87 FPU, which
# evaluates doubles in 80-bit registers -- Tycho float is IEEE-754 binary64, so
# x87 evaluation is not a permitted configuration. -ffloat-store is not enough:
# it rounds on store but not in a comparison.
ilp32: tychoc1
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

ffi: tychoc1
	@sh tests/ffi/run.sh

recursion: tychoc1
	@sh tests/recursion/run.sh

N ?= 200
fuzz: tychoc1
	@python3 fuzz/run.py $(N)

QN ?= 60
fuzz-quick: tychoc1
	@python3 fuzz/run.py $(QN)

fuzz-reject: tychoc1
	@python3 fuzz/run_reject.py $(N)

fuzz-leak: tychoc1
	@python3 fuzz/run_leak.py $(N)

bench-guard: $(TYCHOC)
	@TYCHOC=$(TYCHOC) sh bench/guard.sh

ci:
	@sh scripts/ci.sh $(N)

release-check: tychoc1
	@set -eu; \
	  version="$$(./tychoc --version | awk '{print $$2}')"; \
	  os="$$(uname -s | tr '[:upper:]' '[:lower:]')"; \
	  archive="dist/tycho-v$$version-$$os-$$(uname -m).tar.gz"; \
	  first="$$(mktemp)"; trap 'rm -f "$$first"' EXIT HUP INT TERM; \
	  sh scripts/release.sh "v$$version"; cp "$$archive" "$$first"; \
	  sh scripts/release.sh "v$$version"; \
	  cmp -s "$$first" "$$archive" || { echo "release-check: archives differ" >&2; exit 1; }; \
	  echo "release-check: byte-identical archives ($$archive)"; \
	  win="dist/tycho-v$$version-mingw64-x86_64.tar.gz"; \
	  if ! command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then \
	    echo "release-check: WINDOWS ARCHIVE NOT REBUILT (no mingw here). $$win, if it"; \
	    echo "               exists, is from an earlier build and may be STALE."; \
	  else \
	    sh scripts/release.sh "v$$version" --mingw >/dev/null; \
	    echo "release-check: rebuilt $$win"; \
	  fi

# release-check proves two builds agree byte for byte; it asserts nothing about
# what is IN the archive, which is how a wrong compiler, four .exe files that
# could not start and a missing runtime/ all shipped (commit 7534812f).
release-content:
	@sh scripts/release_content.sh --selfcheck
	@sh scripts/release_content.sh

# ABSOLUTE, not `.githooks`: a relative hooksPath resolves inside whichever
# worktree pushes, and a worktree without a .githooks directory gets no hook
# at all. Run from the main checkout.
hooks:
	@git config core.hooksPath "$$(git rev-parse --show-toplevel)/.githooks"
	@echo "git hooks activated: core.hooksPath -> $$(git config core.hooksPath)"
	@echo "  pre-push: script-check + check-links + version-check + spec-fast + fuzz-quick, and on a gh-pages push, contrast-check + site-code-check"

contrast-check:
	@python3 scripts/check_contrast.py --selfcheck
	@python3 scripts/check_contrast.py

script-check:
	@python3 scripts/check_scripts.py --selfcheck
	@python3 scripts/check_scripts.py

friction-check:
	@python3 scripts/friction_check.py --selfcheck
	@python3 scripts/friction_check.py

surface-check:
	@python3 scripts/surface_lock.py --selfcheck
	@python3 scripts/surface_lock.py

version-check:
	@python3 scripts/check_version_status.py --selfcheck
	@python3 scripts/check_version_status.py

# `make status --net` cannot work: make reads --net as its own flag. The two
# spellings that do are ARGS=--net and this target.
status:
	@sh scripts/status.sh $(ARGS)

status-net:
	@sh scripts/status.sh --net

status-check:
	@sh scripts/status.sh --selfcheck

site-code-check: tychoc1
	@python3 scripts/check_site_code.py --selfcheck
	@python3 scripts/check_site_code.py

# `--emit-c -o <base>` writes and keeps `<base>.c`, so the intermediates below
# are removed here rather than by the build.
clean:
	rm -f tychoc tychoc1 tychoc1-stage1 tycho tycho.c tychofmt tychofmt.c tycho-lsp tycho-lsp.c tycho-debug tycho-debug.c build/tycho_rt_embed.h
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
