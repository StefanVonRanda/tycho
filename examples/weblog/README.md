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
deterministic. Findings 1–3 are corelib gaps; finding 4 is a real compiler bug the
dogfood found and got fixed.

1. **`core:io` has no streaming line reader.** The only option is
   `read_lines(path) -> [string]`, which slurps the whole file into an array, so
   peak RSS tracks input size instead of the bounded aggregation state. A real log
   analyzer over a multi-GB file wants a fold/iterator over lines. This is the
   headline gap — it's the difference between the tool demonstrating the
   value-semantic memory story and defeating it.

2. **`core:datetime` cannot parse the CLF timestamp.** It parses ISO-8601 only
   (`parse_iso`/`parse_iso_tz`); the CLF stamp `[10/Oct/2000:13:55:36 -0700]`
   (month name, `/` and `:` separators, offset) has no parser. The primitives to
   build one exist (`month_name`, `days_from_civil`, `digits_val`), but the hour
   bucket here is just a string slice of the raw stamp, not a real `DateTime` — so
   the histogram can't sort chronologically across a month boundary or do any date
   math. Wants a reusable custom/strptime-style parser.

3. **`core:regex` has no capture groups.** `find`/`find_end`/`matched` return the
   first whole match only — there is no `$1`/`$2` group extraction, and a compiled
   pattern is a raw `ptr` needing a manual `release()` rather than a RAII `handle`.
   So the CLF fields are pulled apart with `strings.split_once`, not a pattern. For
   this rigid format that is arguably clearer, but a general log tool would want
   groups.

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
