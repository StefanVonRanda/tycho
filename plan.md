# What the next program says the language needs

> This plan is a fresh clone, 2026-08-03: the completed tycho-kv plan lives at
> `docs/internals/plan-tycho-kv-DONE.md`. The rule from that plan holds here:
> *does anything that is not the program written to want it need this?* A
> finding becomes a phase only when a second, independent caller exists.

## The program

**A chess engine** -- `tools/tycho-chess/`, bitboards (64-bit ints are the
natural piece map), move generation, make/unmake, perft, and -- if it
survives -- alpha-beta search with a transposition table and a PARALLEL root
search via `parallel for`, the first program to lean on dynamic task fan-out
(the store's pscan had four named spawns because Task handles are affine).

**Phase 1 is DONE (2026-08-04):** the engine and its gate. `main.ty` is ~740
lines: bitboard `Board` (6×2 piece maps + side/castling/ep), attack tables,
FEN parser, pseudo-legal movegen, `make_move` returning a NEW board (value
semantics, no unmake), legality via make + `in_check`, perft with per-root
divide output, CLI `perft <depth> [fen]`. `make chess-check` (in `make ci` as
step [3j/16]) asserts perft totals against PUBLISHED ground truth, and the
divide transcript is golden-locked.

The differential is GROUND TRUTH: perft (legal move counting) has exact
published values for standard positions. Verified totals (GoBit's
perft_tests.txt):
- Start: 20 / 400 / 8,902 / 197,281 / **4,865,609**
- Kiwipete: 48 / 2,039 / 97,862 / **4,085,603**
- Position 3: 14 / 191 / 2,812 / 43,238 / 674,624 / **11,030,083**

perft(5) on the start (4,865,609), Kiwipete(4), and Position 3(6)
(11,030,083) are the deep checks; the divide goldens lock the per-root
counts, which move if any subtree's movegen changes even when the total
doesn't. The gate also asserts five edge positions (bare castling, king
endgames, an en-passant capture, both-side promotions) against the
python-chess oracle -- the standard positions' trees never reach a promotion
or an ep capture, so the published totals alone cannot see those code paths.
~3M nodes/sec on this box; the gate runs ~10s.

**Phase 2 (search) is DONE (2026-08-04):** negamax alpha-beta, quiescence,
PST eval, exact-only transposition table, `search <depth> [fen]` CLI, and the
search assertions in `make chess-check` (determinism, TT-invariance, three
exact tactical probes). **Remaining:** the parallel root search via
`parallel for` -- the untried dynamic fan-out.

Stress points: bitboard manipulation (shifts/masks over int64), move lists
as arena arrays under millions of nodes, and the concurrency phase. The
alpha-beta horizon is shallow, so the recursion guard's workout is modest --
the plan is honest about that.

## Findings

- **The published-total differential caught a bug the start position cannot
  see.** The first cut matched start/kiwipete/pos3 through the shallow
  depths, then drifted: Kiwipete d3 one over, pos3 d2 +23. The cause was the
  four file-edge guards in pawn attack detection, swapped (`sq % 8 != 7`
  where `!= 0` belongs, and vice versa). The start position's tree at d1-5
  never contains the shapes that manifest it -- an h-file king escape that
  must be filtered (h4h3 after e2e3, attacked by the g2 pawn) or a check
  the king must answer (g2g3 checking h4: the unfiltered escape moves
  inflated pos3's d2 by 23). Pass all standard positions at shallow depth
  and still be wrong: the edge FENs (ep, promotion, castling, edge-file
  kings) are what made the guard shape observable. The oracle differential
  (python-chess) and the per-root divide both reddened; the fix was four
  conditions, and all three positions went exact through d5/d4/d6.
- **Operator precedence is documented and C-like: `a & b != 0` parses as
  `(a & b) != 0`** (`docs/spec/appendix-c-precedence.md`: bitwise `&` binds
  at level 3, equality at level 2 -- Go-style). The author initially
  believed the reverse, "fixed" it, and the unchanged totals proved the
  no-op -- the differential settles precedence questions too. No language
  gap: bitboard code needs only `&`, `|`, `<<`, `>>`, `~` and parens, all
  present.
- **Value semantics held on a hot path.** `make_move` returns a whole new
  `Board` (6×2 bitboards + flags, ~100 bytes) per node -- no unmake
  machinery, no move undo stack -- and perft still runs ~3M nodes/sec. The
  copy-per-node design the language's value semantics push toward is not a
  perft liability at this scale.
- **No language gap -- third program in a row.** The engine needed bitboard
  ints, fixed arrays, `&`-masking, struct value copies, string slicing for
  FEN parsing, and match-free branching; all present, zero compiler/runtime
  defects (tycho-kv was the first zero-defect program; this confirms the
  streak). The usual syntax tax was paid again: `int64` does not exist (the
  type is `int`, 64-bit), no hex literals (the castling masks
  6917529027641081856 and 1008806316530991104 stand as decimal), `while` is
  `for cond:`.

## Phases

(none yet -- the program's own milestones below are next; a finding becomes
a phase only when a second, independent caller needs it)

### Next program milestone -- alpha-beta search + transposition table  [DONE 2026-08-04]

The search shipped in the same commit as the engine: negamax alpha-beta with
an exact-only transposition table (below), a PST-based eval (the classic
simplified-evaluation tables), and a quiescence search. `search <depth> [fen]`
reports per-root scores and the best line; `make chess-check` asserts the
search properties. Three bugs were caught on the way, all by the differential:

1. **The negamax leaf sign.** `evaluate()` is white-positive; the depth-0 leaf
   must return the value from the SIDE-TO-MOVE's perspective. Returning the
   unnegated eval inverted every odd-depth-from-white search -- caught by the
   Rxc2 probe (depth 1 said -500 for winning a free queen, depth 2 said +500).
2. **The horizon effect (not a search bug, a missing quiescence).** At depth 6
   from the start every move scored -100: the last ply's eval "won" a pawn
   (3...Nxf7) one ply short of the recapture. Quiescence (captures + checks,
   stand-pat cutoff, delta pruning, depth caps) fixed it.
3. **No quiet-move ordering = no pruning.** order_captures only lifted
   captures; in capture-free positions the alpha-beta degenerated to ~full
   search -- measured 16x per ply (d3 116ms -> d4 1.9s -> d5 26s -> d6 3min).
   PST-aware move ordering collapsed it to the sqrt-shape (d6 3.1s).

The TT is deliberately EXACT-ONLY and same-depth: bound entries are never
stored, so a probe returns only the true minimax value at that depth and the
TT provably cannot change a result -- it cuts transpositions and reorders
moves. The gate asserts exactly that (`search` twice is byte-identical and
`search-nott` reports the same best line) plus three exact tactical probes:
the rook taking a free queen (+510), a hanging queen (+510), and the
scholar's mate (Qxf7# = 100000). The PST-only eval keeps quiet-position
values approximate (depth-5 from the start ties many moves at 0) -- honest,
and the probes pin the parts that must be exact.

### Next program milestone -- parallel root search

The first `parallel for` with dynamic fan-out: the root's moves are searched
in parallel tasks, each a full search of one root move, results merged. The
probe the store's pscan deferred: can `parallel for` scale a dynamic work
set, and do the shared read-only tables (attack tables, Zobrist, PST, TT)
stay safe under concurrent search? Determinism is the gate: parallel search
must report the same best move as serial search. The TT is the concurrency
question -- its map is written per node, so the parallel phase must either
share it read-only after a warmup or give each task its own and merge.
