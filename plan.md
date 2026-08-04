# What the next program says the language needs

> This plan is a fresh clone, 2026-08-04: the completed tycho-chess plan
> lives at `docs/internals/plan-tycho-chess-DONE.md`. The rule from that plan
> holds here: *does anything that is not the program written to want it need
> this?* A finding becomes a phase only when a second, independent caller
> exists.

## The program -- (to be chosen)

The last program (tycho-chess) finished with a question rather than a
finding: its parallel root search worked but paid the full-window cost,
because the language has no shared mutable state for a lazy-SMP alpha.
The next program should pick a stress axis the engine did not touch.

Candidates, honestly scored:

- **RSA / modular bignum arithmetic.** core: bignum exists (`corelib/test/
  bignum` has 2^100 by doubling); a real RSA keygen + sign + verify walks
  big-int multiply, modular exponentiation, GCD/extended-Euclid, and
  primality testing (Miller-Rabin) -- a deep arithmetic workout with exact
  published test vectors (RSA-encrypt a message, compare against known
  ciphertexts). No concurrency, no I/O; a pure-compute stress.
- **An HTTP key-value server.** tycho-httpd and the weblog example exist; a
  second server over core:net (concurrent request handling, a shared store,
  keep-alive) would stress the conc model the chess engine only touched via
  `parallel for`, and the server-check lane pattern already exists. Risks:
  overlapping the existing examples' territory, and the "second program to
  want it" bar for any finding is met more easily here (two net programs).
- **A CHIP-8 interpreter.** a tiny ISA (35 opcodes), a display buffer, a
  timer -- the tycho-vm already proved the VM shape, so this would mostly
  re-prove it; weak stress.

The plan's default is RSA: it exercises arithmetic depth rather than
machinery the language has already shown, and its differential (published
test vectors) is ground truth in the perft sense.

## Findings

- **Bitboard constants are unreadable as decimal.** The tycho-chess engine
  needs castling masks and wrote `6917529027641081856` and
  `1008806316530991104` where every other bitboard engine writes
  `0x6000000000000000`. Integer literals were decimal-only by spec
  (`docs/spec/01-lexical.md:203` now reads "no **octal** form" -- the hex
  absence was the same sentence). The cost was concrete: the constant was
  opaque, and the engine author transcribed it by hand from hex -- one wrong
  digit is a silent bitboard bug that only perft catches if the position
  exercises it. One caller (the engine); the language owner asking for the
  fix is the second, so this is a phase.

## Phases

### Phase 1 -- hex and binary integer literals  [DONE 2026-08-04]

**What shipped:** `src/tychoc.c`'s lexer accepts `0x`/`0X` (hex) and
`0b`/`0B` (binary) integer literals, recognized only when the integer part
is exactly the digit `0` (so `10x` stays an int followed by an identifier,
the C/Go tie-break), always an integer (no hex-float form), with the
overflow check extended to bases 16 and 2. `0x` with no digits and `0b2`
are lexical errors. The spec's §3.9.1 and the generated Appendix A grammar
(regenerated via `scripts/gen_grammar.sh`) describe the new forms; the
parallel-for `0..<N` bound, fixed-array lengths and bounded capacities
needed no changes -- all three consume the token's VALUE, so `0x0..<N`,
`[0xA]int` and `bounded[0x10]T` work as-is. The engine's two castling masks
(`0x6000000000000000`, `0xE00000000000000`) and the ctz masks rewrite to
hex; perft totals unchanged.

**The work:** `tests/int_hex.ty` (values, mixed-case prefixes, 2^63-1
ceiling, float adaptation, `[0xA]int`, `0x0..<3` parallel-for bound) plus
three reject tests (`0x`, `0b2`, `0xFFFFFFFFFFFFFFFF`). Gates: make test,
chess-check, spec-check, the doc gates -- all green.
