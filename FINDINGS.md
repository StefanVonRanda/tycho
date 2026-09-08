# Findings — files on disk

## Scope

- Read: `src/tychoc.c:1-40`, `runtime/tycho_rt.c:1-30`, `OVERVIEW.md:1-60`, `docs/architecture.md:1-100`, `SECURITY.md:1-80`, `ROADMAP.md:1-60`, `plan.md:1-60`
- Measured: `src/tychoc.c:15202` lines, `runtime/tycho_rt.c:3193` lines, `compiler/:21320` lines (`compiler/emit/emit.ty:8113`, `compiler/types/tcheck.ty:3879`, `compiler/parse/parse.ty:2632`)
- Ran: no gates (read-only review)

## Layout

| Area | Files |
|---|---|
| Bootstrap compiler | `src/tychoc.c:1` |
| Self-hosted compiler | `compiler/main.ty:1`, `compiler/lex/lex.ty:1`, `compiler/parse/parse.ty:1`, `compiler/types/tcheck.ty:1`, `compiler/emit/emit.ty:1`, `compiler/driver/driver.ty:1` |
| Runtime | `runtime/tycho_rt.c:1` |
| Stdlib shims (largest) | `corelib/io/io_shim.c:1`, `corelib/net/net_shim.c:1`, `corelib/crypto/crypto_shim.c:1`, `corelib/os/os_shim.c:1` |
| Surface lock | `surface.lock:1` |

## Gaps

| ID | Location | Observed |
|---|---|---|
| G1 | `src/tychoc.c:2662` | Sibling-import check sees only files parsed before current file |
| G2 | `src/tychoc.c:5367` | Import-used marking not file-scoped; dead import missed when sibling uses same package |
| G3 | `src/tychoc.c:5852` | One diagnostic self-check has no gate; regression to spelling test invisible |
| G4 | `compiler/driver/driver.ty:168` | Flags split on spaces; path with space breaks |
| G5 | `compiler/parse/parse.ty:25` | CLOSED: recovery extended to where-on-non-generic, missing ':', missing newline after ':' |
| G6 | `compiler/parse/parse.ty:120` | PARTIAL: compile path and parse-only path covered; emit f-string holes still uncovered |
| G7 | `compiler/parse/parse.ty:1959` | CLOSED: _spanttxt skips whitespace tokens (defensive; spaces are not tokens in Tycho) |
| G8 | `compiler/types/resolve.ty:893` | Corelib hint missing for one resolve path (names `src/tychoc.c:6077`) |
| G9 | `compiler/emit/emit.ty:279` | Local bounded annotation not tracked; over-capacity push grows |
| G10 | `compiler/emit/emit.ty:4869` | Two or more reductions need tuple return; single reduction only |
| G11 | `corelib/os/os_shim.c:225` | Batch files not refused |
| G12 | `corelib/os/os_shim.c:380` | Windows path untested; no Windows CI for this file |
| G13 | `corelib/httpd/httpd.ty:145` | 15 significant digits ceiling (10^15 bytes) |
| G14 | `tools/tycho-debug/main.ty:208` | Separator normalization only; drive-letter case not handled |
| G15 | `tools/tycho-make/graph/graph.ty:82` | No variables, expansion, pattern rules, PHONY |
| G16 | `tools/tycho-make/graph/graph.ty:84` | Comment only as first non-blank character |
| G17 | `tools/tycho-make/graph/graph.ty:87` | Trailing backslash refused, not joined |
| G18 | `tools/tycho-make/build/build.ty:11` | Hash via text read; non-text targets out of scope; no intra-node parallelism |

## Non-gaps (name collision, not debt)

| Location | Observed |
|---|---|
| `tools/tycho-ed/buf/buf.ty:28` | `gap` is a struct field (editor backend flag), not a debt marker |
| `tools/tycho-ed/main.ty:119` | `gap` is a function parameter, not a debt marker |

## Comment rules (from last agent)

| Rule | Check |
|---|---|
| No prose in comments | `python3 scripts/check_citations.py` |
| No future-assertions in comments | `sh scripts/check_links.sh` |
| Quote `path:line` for definitions | `python3 scripts/check_citations.py --report` |
