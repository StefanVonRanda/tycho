# weblog — an access-log analyzer, as a corelib dogfood

A small [Combined Log Format](https://httpd.apache.org/docs/current/logs.html#combined)
analyzer written entirely against corelib. It reads an access log, parses each
record fail-closed, aggregates into string-keyed maps, and reports status
classes, the top-K URLs and client IPs, and a per-hour request histogram.

```
tychoc examples/weblog/main.ty -o weblog
./weblog access.log            # analyze a file
./weblog access.log --top=10   # show top 10 instead of 5
./weblog                       # no file -> analyze the embedded demo log
```

With no file argument it analyzes an embedded demo log so the output is
deterministic and can be golden-locked (`sh examples/weblog/run.sh`). `access.log`
in this directory is the same content as the embedded demo.

This exists less as a tool than as a **dogfood**: build something real against
corelib and let every rough edge become a ticket. It touches `core:cli`,
`core:io`, `core:strings`, `core:sort`, the map builtins, and string-keyed maps
under a real aggregation load (this is the compact indexed-dict map layout doing
actual work). What it reached for and couldn't get cleanly is the useful output.

## Dogfood findings

Ordered roughly by value. The tool compiles and runs identically on **both**
compilers (`tychoc` and the self-hosted `tychoc0`), output correct and
deterministic. Four of these have since been fixed by the dogfood: finding 1 (a
`core:io` streaming reader), finding 2 (a `core:datetime` CLF parser), finding 3
(`core:regex` capture groups), and finding 4 (a real tychoc0 compiler bug). Only
finding 5 remains open.

1. **`core:io` had no streaming line reader — fixed.** Originally the only option
   was `read_lines(path) -> [string]`, which slurps the whole file into an array, so
   peak RSS tracked input size instead of the bounded aggregation state — defeating
   the value-semantic memory story on a large log. Fixed by adding a bounded-memory
   reader to `core:io` over a libc `getline` shim: `open_lines` → `read_line`
   (`Some`/`None`) → `close_lines`, plus a `fold_lines(path, init, f)` convenience.
   This program now streams the log one line at a time (peak memory O(longest line),
   not O(file)); the embedded demo is materialized to a temp file so even the
   default run flows through the same path.

2. **`core:datetime` could not parse the CLF timestamp — fixed.** It parsed
   ISO-8601 only (`parse_iso`/`parse_iso_tz`); the CLF stamp
   `[10/Oct/2000:13:55:36 -0700]` (3-letter month name, `/` and `:` separators, a
   colon-less `±HHMM` offset) had no parser, so the hour bucket was a raw string
   slice — not a real `DateTime`, so it couldn't sort chronologically or do date
   math. Fixed by adding `parse_clf` (civil fields) and `parse_clf_tz` (offset
   folded to the UTC instant), plus `month_num` (the inverse of `month_name`),
   reusing the same day-count core. The bucket is now a validated, sortable
   `YYYY-MM-DD HH` derived from the parsed `DateTime`; a malformed timestamp fails
   the record closed.

3. **`core:regex` had no capture groups — fixed.** `find`/`find_end`/`matched`
   returned the first whole match only, with no `$1`/`$2` extraction. Fixed by
   adding `ngroups`, `group_start`/`group_end`, `group(re, s, n)` (0 = whole
   match), and `groups(re, s)` over POSIX `regexec`'s `pmatch[]`. This program
   still parses the rigid CLF layout with `strings.split_once` (clearer for a fixed
   format), but a general log tool can now use groups. (One minor point remains: a
   compiled pattern is a raw `ptr` with manual `release()`, not a RAII `handle`.)

4. **A real tychoc0 bug — found here and fixed.** `parse_line` splits the CLF
   timestamp (`dpart, trest := split_once(ts, ":")`) and later builds
   `datehour := dpart + ":" + hour`. tychoc resolved that fine; `tychoc0` reported
   `dpart` as an unknown variable. Root cause: tychoc0's `lift`/`mono`/`gfix`
   statement walks recursed past an `SDestructure` node without recording its bound
   names, so `type_of` on a *later* declaration that read one couldn't see it. It
   only bit when a destructure var fed an intermediate `:=` decl — a shape
   `tychoc0.ty` itself never contains, so the self-hosted compiler compiled cleanly
   and the output-only fixpoint differential never saw the gap. Fixed in all three
   passes (mirroring how `gen_stmt` already tracked them), locked by
   `tests/destructure_scope`. This is the dogfood's headline result: a real
   parity/soundness bug that the whole test + fuzz + fixpoint gate had missed,
   caught by writing one real program.

5. **`tychoc` package-mode diagnostics misattribute.** Every parse error in this
   package build printed the wrong file, line, and source snippet (e.g. a field-
   name error in `main.ty` rendered as a line from `corelib/sort/sort.ty`). The
   error *category* was right; the location was not. Single-file builds are fine —
   this is specific to the merged package build. (Still open.)

6. **Map "increment-or-insert" is a three-line idiom.** `if k in m: m[k] = m[k] + 1
   else: m[k] = 1`, repeated for every counter. A tiny ergonomic (a `map_inc`
   builtin, or an entry-style API) would remove a lot of noise. (Compounded here by
   #4, which blocks the obvious `inout` helper.)

## Gotchas hit along the way (not bugs)

- **`args()` includes `argv[0]`** (the program path). `core:cli.parse` expects it
  already stripped, so the program drops the first element before parsing —
  otherwise the binary's own path is read as a positional and the tool tries to
  parse *itself* as a log.
- **`bytes` is a reserved type keyword**, so a struct field cannot be named
  `bytes` (it parses as a type, giving "expected a field name"). The field is
  `size`.
- **Empty map literal is `[]string: int`** (the `[]` mirrors `[]int` for arrays),
  not `[string:int]` (that is the *type* form).
