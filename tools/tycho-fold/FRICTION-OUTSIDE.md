# Friction writing `tycho-fold`, from outside

The **third** `ROADMAP.md` §1 program: wrap text to a width, counting codepoints.
Written 2026-08-15 from `docs/` by someone who did not write the compiler.

The three were chosen to land on different surfaces on purpose —
`tycho-diff` a sequential algorithm, `tycho-hash` concurrency, this one UTF-8 —
because a longer program on one surface would have repeated its own findings.
**Three programs now exist. §1 asks for three by TWO people, and all three are by
one non-author, so the item is not closed.**

---

## 1. `utf8.decode` returns a tuple, and a zero-length step is a live hazard

```tycho
cp, nb := utf8.decode(line, i)
if nb <= 0:
    nb = 1        # consume the byte anyway, or the loop never terminates
```

The API is right — `(codepoint, nbytes)` is what a decoder should return, and the
tuple destructures cleanly. What is not obvious from the signature is that a
caller who trusts `nb` on invalid input writes an infinite loop, and the input
that triggers it is by definition the hostile one. `utf8.valid` exists to check
first, but a streaming caller cannot afford a second pass.

Nothing to fix in the library; worth a line in its doc comment.

## 2. `len(s)` is bytes, and every ergonomic path leads to it

`len` is the obvious call, `s[i]` is the obvious index, and both are byte-based.
The codepoint count is `utf8.count(s)`, in a package you must import separately.
That is the correct layering — a string is bytes — but it means the *natural*
program is the wrong one, and it is wrong only on input the author probably did
not test with.

This program keeps the byte-counting path **reachable** behind `--bytes` for
exactly that reason: it makes the difference assertable instead of assumed.

## 3. What went right

**Nothing in the language fought the UTF-8 work.** Slicing by a decoded length
(`line[i:i + nb]`) is natural, the tuple destructure reads well, and the compiler
caught the type errors while I got the algorithm wrong on my own.

**The property test found what reading could not.** Three properties over 200
generated lines mixing ASCII, Latin-1, CJK and emoji at widths 3..30: nothing
lost, nothing over the width, output still valid UTF-8.

---

## Two mistakes, both mine, both in the CHECK rather than the program

Recorded because this is now three programs in a row where the checking was
harder to get right than the code.

**The first check was wrong and accused working code.** Comparing the *word set*
of input and output flagged 111 of 120 lines — every one a legal hard break of a
word longer than the width. The invariant that survives hard-breaking is the
concatenation of non-space characters, in order.

**The second check was right and found a real defect.** `if wn > w` broke *after*
passing the width, emitting `w+1` characters on every long word: **596 over-width
lines** across 200 inputs, and completely invisible by eye because the output
still looks like wrapped text. Fixed to `>= w`.

**And the lane's own fixture was wrong.** `printf '\xff\xfe'` under `/bin/sh` —
dash here — does not interpret hex escapes, so the "invalid UTF-8" fixture was
valid UTF-8 and leg [8] failed while the program was correct. Octal (`\377\376`)
works in both shells.

Three programs, three times the instrument was the thing that needed fixing
first. That is the finding underneath the findings.
