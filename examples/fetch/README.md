# fetch — a corelib dogfood

A small CLI that GETs a URL and summarizes + caches the response, composing
**five** corelib modules end-to-end:

| module        | role                                                |
|---------------|-----------------------------------------------------|
| `core:http`   | the request (any libcurl protocol: http/https/file) |
| `core:json`   | detect + summarize a JSON body (kind, top-level keys) |
| `core:sha256` | a content hash of the body                          |
| `core:io`     | write the body to a content-addressed cache file    |
| `core:path`   | the URL's basename and the cache filename           |

```
$ fetch https://example.com
source : example.com
status : 200
bytes  : 559
sha256 : ff67a9d764d6a2367a187734e697f6a53217db9a21c101d410a113ca871a299d
content: text, 559 bytes
cached : tycho_fetch_ff67a9d764d6a236.txt
```

The whole document lives in a per-scope arena and is freed when `main` exits —
no `malloc`/`free`, no reference counts, no GC anywhere in the pipeline. That is
the thesis (value semantics + implicit arenas) on a realistic, allocation-heavy
program rather than a micro-benchmark.

## Running

`make fetch` (or `sh examples/fetch/run.sh`) builds the program by **both**
compilers (the C `tychoc` and the self-hosted `tychoc0`), runs each against a local
`file://` fixture so the whole pipeline is exercised **deterministically and
offline**, asserts byte-identical output against `expected.out`, and re-runs the
emitted C under ASan/UBSan (proving the transient libcurl response body is
copied into the arena before the handle is freed — no use-after-free). It skips
cleanly if libcurl is absent. A real `https://` GET is verified by hand.
