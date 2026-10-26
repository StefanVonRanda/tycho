<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-<version>-*.tar.gz dist/tycho-<version>-*.sha256 --notes-file RELEASE_NOTES.md
  gh release edit <version> --prerelease
Build one tarball per platform (there is no hosted CI); attach them all to the release.
Name the version in the globs: dist/ still holds the previous release's tarballs.
-->

Tycho 0.9.0 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**0.9.0 is the managed-performance and compiler-parity checkpoint.** The
language surface is unchanged; this release makes Tycho competitive with the
best managed implementations by measurement, closes the tracked gap between the
C bootstrap and the self-hosted compiler, and removes the remaining CLI hash
bottlenecks.

## Managed performance

The generated suite covers **18 workloads at three sizes**, with Go, Java and
Swift ports where the workload is meaningful. The release gate requires every
row to be within **1.10x** of the best managed time or memory on at least one
backend, with a **1.15x** self-regression limit and a repetition-noise check.
C and Rust are reported as native diagnostics, not release blockers.

Using large-size geometric means under GCC, Tycho measures **0.40x Go time** and
**0.50x Go memory**; against Java the figures are **0.45x time** and **0.05x
memory**, and against Swift **0.18x time** and **0.51x memory**. The clang
report is similar (0.37x Go time, 0.50x Go memory). All compared rows pass the
managed gate.

The suite also gained specialized rows and managed ports:

- specialized `trie`, `dijkstra`, `lru`, `interp` and `json_tree` rows now have
  Java and Swift peers;
- Rust/C service, SQLite, latency and specialized rows are measured for
  context;
- `binary/arena`, `dispatch/expert` and `maptree/arena` remain explicit control
  variants without managed ports.

Memory-heavy rows improved substantially:

| workload | before | 0.9.0 |
|---|---:|---:|
| `trie` | 132.7 MB | **76 MB** |
| `json_tree` | 107 MB | **81 MB** |
| `gcscan` | 372 MB | **127 MB** |
| `invindex` growth rows | — | **150–194 MB** |

## Compiler and runtime

- **Reference-compiler parity is complete for the tracked features.** The C
  bootstrap now has the closure ABI, simple-place calls, counted/while
  presizing, loop-scope elision, inline scalar-payload enums and nested push
  destination inference. Its generated suite is checksum-clean. The saved
  benchmark reports still measure the self-hosted `tychoc1` as the optimized
  path.
- **Inline scalar-payload enums** are emitted as a packed tag plus variant
  union in both compilers instead of a pointer to an arena cell. Recursive,
  generic, fieldless, package-mangled and heap-payload enums keep the pointer
  layout. `TYCHOC_NO_INLINE_ENUM=1` restores the old representation for
  measurement.
- **Closure environments and call descriptors** are inlined for small
  functions; loop-local arrays move into returned push destinations; counted
  `for` and canonical `while` builders are presized from their bounds. These
  remove geometric growth and closure-scope allocations from the hot rows.
- **Map and list churn** is now competitive: read-only map lookups borrow,
  unshared map writes avoid a copy, integer `push` has an inline fast path, and
  integer map keys use a collision-free permutation hash. The `winagg` row
  dropped from ~165 ms to ~84 ms against C ~97 ms.
- **JSON and map layout** became flatter: composite map headers are smaller and
  backed by a nullable data block; string map keys and repeated JSON keys are
  interned; the fast parser builds a compact field spine directly; repeated
  key/value strings are sliced once. `Json.JObj` now carries a single
  `[JsonField]` array; see **Compatibility**.

## CLI and real programs

- **`tycho-hash` no longer deadlocks or grows with the file count.** A producer
  task feeds bounded 32-slot queues while the main thread drains results, which
  preserves the 96-file stress fix without scaling memory with the tree size.
- **Whole-file hashing is hardware-accelerated when available.** The runtime
  uses SHA-NI first, optional OpenSSL `SHA256` next, and scalar C otherwise.
  The pure Tycho streaming implementation remains the portable API.
- On a deterministic **512-file / 128 MB** tree with eight workers, Tycho now
  hashes in **0.01 s / 10.8 MB** versus Go's **0.01 s / 7.1 MB**, down from
  74.7 MB before the channel fix.
- The **production multi-worker server** serves static files at **118.8k
  requests/s / 3.5 MB**, versus Go `http.FileServer` at **84.9k / 17.7 MB**.
  The minimal single-loop fixture is intentionally not the recommended server
  shape; it does not scale with connections.
- The real-program pass also covers concurrency rows, SQLite (`dbquery` within
  ~2% of C and ahead of the managed ports), and large JSON documents (tied with
  Java on time and far below it on memory).

## Compatibility

No language syntax or value-model change. `surface.lock` gained only the
additive `sha256.hex_bytes`. The one known breaking corelib change is
**`corelib/json`'s object representation**:

- before: `JObj([string], [Json])` with parallel key/value arrays;
- now: `JObj([JsonField])`, where `JsonField` holds `k: string` and `v: Json`.

Code that constructs or pattern-matches `JObj` directly must be updated. Code
that uses the parser, serializer and accessors is unaffected. `sha256.hex_bytes`
is additive.

## Verification

- `make test` — 1163 passed
- `make corelib` — 49 ok
- `make fixpoint-check` — `gen2.c == gen3.c`
- `make bench-suite-gate` — every compared managed row within 1.10x
- `make external-check` — deterministic hash reports, worker counts and error
  paths
- `make docs-fences` — 245 snippets, 0 failures
- `make fuzz-quick` — 60 seeds across native and ASan/UBSan builds, 0 failures
- `make asan-self` — 1171 compiler programs, 0 failures
- GCC and clang backend reports; Windows cross-build check

## Known gaps

- **CLI hash process memory** is ~1.5x Go's. SHA throughput is no longer the
  cost; the remaining working set is the per-worker file buffer and runtime
  state.
- **Minimal single-loop server** scaling remains a documented limitation. Use
  the production multi-worker server.
- **Native C/Rust** still lead on some pointer-heavy or recursive workloads,
  most visibly `json_tree`, `trie` and `dijkstra`. This is a diagnostic, not a
  managed-language loss.

0.9.0 is pre-1.0 and there are **no stability guarantees**: anything here may
change before 1.0. See [CHANGELOG.md](CHANGELOG.md) for the accumulating record.
