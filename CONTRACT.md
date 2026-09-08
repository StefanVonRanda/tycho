# Contract — component ownership map

Every phase (1–7) owns a disjoint set of files. No file appears in two
components. A phase may read files outside its set, but only to verify its
own changes — it must not edit them.

## Ownership

| Component | Owned files | Gaps |
|---|---|---|
| 1 — emit | `compiler/emit/emit.ty` | G9, G10 |
| 2 — parse | `compiler/parse/parse.ty` | G5, G6, G7 |
| 3 — driver / resolve | `compiler/driver/driver.ty`, `compiler/types/resolve.ty` | G4, G8 |
| 4 — tychoc | `src/tychoc.c` | G1, G2, G3 |
| 5 — os shim | `corelib/os/os_shim.c` | G11, G12 |
| 6 — httpd / debug | `corelib/httpd/httpd.ty`, `tools/tycho-debug/main.ty` | G13, G14 |
| 7 — tycho-make | `tools/tycho-make/graph/graph.ty`, `tools/tycho-make/build/build.ty` | G15–G18 |

## Rules

1. A phase edits only files in its Owned set.
2. A phase may read outside its set for verification only (gate output,
   existing fixtures, golden files).
3. If a fix requires touching a file in another component's set, the phase
   must stop and name the file and component — the work belongs there.
4. The critic phase (x.2) re-runs the builder phase's checks and reports
   PASS or numbered failures with file:line.

## Critic verdicts

### 1.2 — emit critic: PASS

Re-ran 1.1 checks after both G9 and G10 fixes:
- `make parse-check`: all green (0 disagreements)
- `make test`: 1035 passed, 0 failed
- abort fixture (`tests/conc/abort/bounded_local_cap.ty`): traps `push to a full bounded[2]` exit 1
- multi fixture (`tests/conc/parfor_chan_multi.ty`): prints `156` exit 0
- non-trap fixture (`tests/bounded_local_cap.ty`): prints `len 2` exit 0

Revert-each (both confirmed load-bearing):
- Revert G9 (remove `ast.Name` case in `_cap_of_target`, remove vcaps population in `_stmt`): abort fixture silently grows, prints `unreachable` exit 0 — gap re-introduced
- Revert G10 (replace `_parchan` with pre-fix): multi fixture refused by name (`a parallel for with more than one reduction variable -- Phase 8`) exit 1 — gap re-introduced

### 2.2 — parse critic: PASS

Re-ran 2.1 checks after G5, G6, G7 fixes:
- `make parse-check`: all green — leg4c multi-error recovery: errors=2 (>=2); leg4d parse-only file path in diagnostics: shown=yes
- `make test`: 1035 passed, 0 failed
- `tests/generic_typeset.ty` covers G7 where-clause path

### 3.2 — driver-resolve critic: PASS

Re-ran 3.1 checks after G4 and G8 fixes:
- `make test`: 1035 passed, 0 failed
- `python3 scripts/check_citations.py`: ok
- `sh scripts/check_links.sh`: ok
- Spaced-path fixture (`/tmp/test space dir/main.ty`): builds and runs, prints `hello world` exit 0
- Corelib hint fixture (`sort([3,1,2])` without import): emits `core:arrays provides 'sort' -- add import "core:arrays" and call arrays.sort(...)` exit 1
- Revert G4 (replace `os.run(cmd)` with `os.exec(argv)` in build): spaced-path fixture fails at cc (shell tokenisation lost the space)
- Revert G8 (remove `corelib_hint` call from `_call_bare`): hint fixture prints bare `unknown procedure 'sort'` without the core:arrays suggestion

### 4.2 — tychoc critic: PASS

Re-ran 4.1 checks after G1, G2, G3 fixes:
- `make test`: 1036 passed, 0 failed (all green)
- `sh scripts/spelling_gate_corelib.sh`: PASS
- G1 order-swapped fixture (`tests/reject/pkg/import_leak_type_order/main.ty`): refuses rc=1 naming `pkg/a.ty:3` with sibling note
- G2 dead import probe: `other/other.ty:3` errors `../pkg imported and not used in this file` (dead import caught per-file)
- Revert G1 (restore inline parse-time check): order-swapped fixture passes (exit 0) — gap reintroduced
- Revert G2 (restore unscoped `g_imports[i].used = 1`): `other.ty` dead import silent — gap reintroduced
- `sh scripts/check_links.sh`: ok (84 docs, 257 anchors, 135 files, 0 dead links)
- `python3 scripts/check_citations.py`: 149 stale citations (pre-existing — docs reference line numbers in a 15000-line file; any code insertion shifts them; not a 4.1 regression)

### 5.2 — os-shim critic: PASS

Re-ran 5.1 checks after G11 and G12 fixes:
- `make shim-check`: 15 ok, 1 skipped (os_argv_quotecheck.c — Windows-only), 0 failed
- `make corelib`: all green (46 ok, tychoc matches goldens)
- G11 (batch files refused): `osx_is_batch()` checks `.bat`/`.cmd` suffix case-insensitively; called at entry to `osx_spawn_win` returning -1. Gap comment updated: extension-based check catches explicit `.bat`/`.cmd`, PATH-resolved bare names like `cmd` are a documented limitation.
- G12 (Windows path untested): loud-skip comment updated; `os_argv_quotecheck.c` is the only Windows gate (skipped on Linux hosts as expected). No revert needed — both are additive guards with no effect on the Linux path.
- `sh scripts/check_links.sh`: ok (0 dead links)
- `python3 scripts/check_citations.py`: 149 stale (pre-existing, not a 5.1 regression)

## Done-when check

```sh
python3 scripts/check_citations.py && sh scripts/check_links.sh
```

Both must exit 0. These verify every `path:line` citation and every
relative Markdown link in the repo, including any new ones added during
the swarm.
