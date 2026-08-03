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

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
