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

## Done-when check

```sh
python3 scripts/check_citations.py && sh scripts/check_links.sh
```

Both must exit 0. These verify every `path:line` citation and every
relative Markdown link in the repo, including any new ones added during
the swarm.
