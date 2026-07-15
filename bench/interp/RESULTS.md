# Tree-walking interpreter — tycho vs C vs Go

Build a large deterministic expression AST (a **recursive enum** / tagged union), eval
it, **constant-fold** it (a rewrite pass that allocates a NEW AST), count nodes, and
deep-compare original vs folded. Each port folds the results into a checksum; the
generator (an LCG threaded through the recursion) and eval are identical, so the
checksum is byte-identical across all three. The AST is the memory under test.

- **tycho**: a value-semantic `enum Expr { Lit, Bin, If }` — recursive (a `Bin` holds two
  `Expr`), heap-cell per node, bump-allocated in an arena, no GC and no manual frees.
- **C**: the textbook tree-walker — a tagged union `struct Expr` with a `malloc`'d node
  per AST node (build-and-hold, no frees), compiled `-fwrapv` so eval wraps like tycho.
- **Go**: `*Expr` struct pointers, GC-managed.

At depth 20 the generator builds **5.58M** nodes; constant-folding produces a **2.99M**-node
copy (it collapses constant subtrees), so ~8.58M nodes are live across the run.

## Numbers

Machine: AMD Ryzen 7 7735HS, x86-64, Linux. tycho + C at `cc -O3` (C also `-fwrapv`),
Go at `go build`. Peak RSS via `getrusage` (`bench/peakrss.c`); best-of-N wall.
Run: `sh bench/interp/run.sh`.

| lang  | peak RSS | time   | checksum (n eval foldN foldEval same)                 |
|-------|----------|--------|-------------------------------------------------------|
| tycho | 252.0 MB | 775 ms | `5581540 -1421683895231085032 2994746 ... false`      |
| C     | 512.2 MB | 503 ms | `5581540 -1421683895231085032 2994746 ... false`      |
| go    | 539.6 MB | 684 ms | `5581540 -1421683895231085032 2994746 ... false`      |

(`same = false`: folding changes the tree, so `t != fold(t)`. `foldEval == eval`: folding
is semantics-preserving.)

## Verdict — the arena WINS on recursive enums (~0.5× C, ~0.47× Go)

This is the inverse of the trie. There, each node owned an `int -> child` **map**, whose
header + backing arrays made tycho ~1.55× C (was ~3.2× before the compact map layout). Here each node is a plain recursive cell with
no per-node map — a **value-shaped, arena-friendly** structure — and tycho uses **half the
memory of C and Go**:

- **No per-node allocation header.** C `malloc`s every node; glibc adds ~16 B of metadata
  per allocation, so ~8.6M nodes carry ~137 MB of pure bookkeeping. tycho bump-allocates
  nodes densely in an arena — zero per-node header.
- **No GC metadata.** Go's GC tracks every pointer-bearing object; its heap for the same
  8.6M-node forest is the largest of the three.
- **Dense cells.** A tycho enum cell is `{ tag; payload }` packed by the arena; the C
  `struct` pays union + alignment padding on top of the malloc header.

Time is competitive — ~1.5× C, faster than Go — the recursion + arena allocation against
C's raw `malloc`/pointer-chase.

The honest framing: value semantics + implicit arenas are **strongest exactly here** —
a large, recursive, heap-dense structure with no per-node side tables. It is the same
model that costs ~3.2× on the trie (per-node maps) paying off when the shape is a clean
recursive value: a tree-walking interpreter's AST is the canonical case, and tycho gets
**C-class-or-better memory with no GC and no manual frees**.

This also doubled as an audit of the recursive-enum machinery (build / eval / fold /
count / deep `==`, plus enums in arrays / maps / options / struct fields / closures): both
compilers byte-identical, ASan/UBSan clean. No bugs found — the enum value-shape is solid.

NOT wired into `make ci` (a head-to-head dogfood, like `bench/dijkstra`). The Go port is
skipped automatically when `go` is absent.
