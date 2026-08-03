# What the next program says the language needs

> This plan is a fresh clone, 2026-08-03: the completed tycho-kv plan lives at
> `docs/internals/plan-tycho-kv-DONE.md`. The rule from that plan holds here:
> *does anything that is not the program written to want it need this?* A
> finding becomes a phase only when a second, independent caller exists.

## The program

(TBD -- candidates under investigation.)

- **A chess engine** (recommended): move generation, alpha-beta search with a
  transposition table, evaluation. The natural stress it brings that no
  program so far has: the recursion guard's real ceiling (a game-tree search
  recurses as deep as the search horizon), parallel root-move search via
  `parallel for` (the first program to lean on DYNAMIC fan-out -- the
  store's pscan had four named spawns because Task handles are affine), and
  bitboard manipulation. The differential is against GROUND TRUTH: perft
  (move-counting) has exact published values for standard positions, so the
  engine's answers are checked against known numbers, not a naive reference.
- **A concurrent KV-store over HTTP**: the tycho-kv store behind the
  existing HTTP server, with concurrent request handling -- combines pieces
  rather than stressing new territory.
- **An RSA tool on core: bignum**: modular exponentiation, key generation --
  exercises the existing bignum library; less novel.

The choice belongs to whoever starts the plan; the program section is filled
when picked.

## Findings

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
