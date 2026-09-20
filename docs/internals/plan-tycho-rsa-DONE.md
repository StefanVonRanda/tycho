# What the next program says the language needs

> The program this plan built was DELETED on 2026-09-04: no constant-time
> routine can be written in Tycho, so a safe RSA would have to be a C shim.
> This file is kept as the record of what the exercise taught the language.

> This plan is a fresh clone, 2026-08-04: the completed tycho-chess plan
> lives at `docs/internals/plan-tycho-chess-DONE.md`. The rule from that plan
> holds here: *does anything that is not the program written to want it need
> this?* A finding becomes a phase only when a second, independent caller
> exists.

## The program -- RSA in pure Tycho  (tools/tycho-rsa/)

RSA key generation, encryption/decryption and sign/verify built on
`core:bignum` (which has add/sub/mul/divmod but NOT modular exponentiation,
GCD or primality — those are the work). The language stress is arithmetic
depth: four programs in, none has done real numeric computation, and bignum
is the least-exercised corelib (its test is 2^100 by doubling).

The differential is GROUND TRUTH, in the perft sense: RSA has exact published
vectors. The gate asserts: the textbook key (p=61, q=53, n=3233, e=17,
d=2753: encrypt 65 → 2790), modexp values cross-checked against python's
pow() at 256/512/2048-bit sizes, and a deterministic-seeded keygen whose
output (n, e, d, p, q), structural invariants (n = p·q, e·d ≡ 1 mod φ,
p/q prime) and an encrypt→decrypt round-trip are all golden-locked.

Scope, honestly sized: raw RSA (textbook) — no PKCS#1 padding, noted as a
deliberate omission (padding is a protocol layer, not arithmetic).

## Phases

### Phase 1 -- the arithmetic: modexp, gcd, modular inverse, Miller-Rabin  [DONE 2026-08-04]

Built on core:bignum, which had add/sub/mul/divmod and nothing else:
`modexp` (square-and-multiply over the Big exponent's bits, low bit = lowest
limb's low bit since BASE = 10^9 is even), `gcd` (Euclid), `inv_mod`
(iterative extended Euclid), `is_prime` (trial division by the first 15
primes, then Miller-Rabin with the first 12 as witnesses -- probabilistic at
our sizes, honestly stated: the gate's round-trip is the final arbiter, not
is_prime). The language held: no findings. The only tax was qualification
(the corelib types are `bignum.Big`, not `Big`).

### Phase 2 -- keygen + the gate  [DONE 2026-08-04]

`gen_key` (two distinct half-size primes, e = 65537, d = e^-1 mod phi,
retrying the pair until gcd(e, phi) == 1), keygen deterministic via
`rand.seed` with a fixed constant so the whole selfcheck transcript is a
reproducible golden. `make rsa-check` (ci step [3k/17], ~4s) asserts: the
textbook vector (p=61 q=53 n=3233 e=17 d=2753: encrypt 65 -> 2790), modexp
cross-checked against python 3 pow() at 256/512/2048 bits, Miller-Rabin
probes (97 prime, 91 composite, 561 -- the Carmichael number, which passes
Fermat but must fail MR), and the deterministic keygen with n == p*q,
e*d == 1 mod phi, p/q prime, and encrypt->decrypt + sign->verify round-trips
-- all golden-locked -- plus a 512-bit keygen round-trip through the CLI
(mul/divmod at 16 limbs). Measured: 256-bit keygen in the selfcheck ~0.2s;
512-bit 1.8s; 1024-bit 8.3s -- schoolbook O(n^2) scaling, as expected from
core:bignum's mag_mul.

**No findings to file** -- the arithmetic-stress axis the plan predicted
(real numeric computation on core:bignum) held without a language change.

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
