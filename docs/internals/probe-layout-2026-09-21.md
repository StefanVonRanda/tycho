# Layout/SIMD probe, 2026-09-21 — eight findings on the surface that returned none in September

[probe-simd-2026-09-07.md](probe-simd-2026-09-07.md) aimed at `packed`,
`align(N)`, `vector[N]T` and swizzling and came back with **zero findings**. This
is the same four features, two weeks later, and it came back with eight. The
difference is not the model and not the surface: that probe wrote a 57-line pixel
kernel, this one wrote a program with an independent checking harness and then
tried to break each feature on purpose. **A demo-sized program on a surface is
not evidence about that surface**, which is the most useful thing this round
established and it is about the instrument, not the language.

Host: the macOS arm64 laptop, `tychoc 0.8.5` from `main`.

## The program

`tgagrade` — a per-channel gain/bias colour grader for 32-bit uncompressed TGA.
`packed struct TgaHeader` (18 bytes with a `u16` starting at byte 3, i.e.
deliberately misaligned), `align(8) struct ChanStat` in a `[4]`, `vector[4]f32`
for `q := px * gain + bias`, and `px.(r, b) = px.(b, r)` as the real BGRA↔RGBA
conversion rather than a demonstration.

**The checking is the part worth copying.** Three independent oracles, none of
them the program: Python's own `struct.pack`/`unpack_from` writes the input and
re-parses the output header field by field; a float64 recomputation compares every
pixel; and macOS `sips` decodes the output TGA to PNG, which a hand-written
inflater then compares to the same expectation. Plus `cc` measuring the emitted
structs directly. A 256×256 sweep covers every byte value on every channel.

Then it **broke each feature on purpose and recorded what the checks said** —
deleting the swizzle fails 30 of 32 pixels, widening one `u8` header field to
`u16` shifts `size_of$` to 19 and trips the program's own validation, lying about
the origin byte is caught by the third decoder and by nothing else. It also
reports a control that turned out not to be one: swapping `width` and `height` in
the declaration changes nothing observable, because the program reads and writes
through the same struct and the error cancels. **A round-trip through your own
layout cannot detect a symmetric layout error.**

## The score

| # | finding | verdict |
|--:|---|---|
| 7 | on arm64 every wide `vector` warns about x86-64 and prescribes `--target x86-64-v3`, which `cc` then rejects on that same host | **FIXED** `e99b8484` — [FRICTION 124](FRICTION.md) |
| 2 | "`packed` does not mean packed in memory" — the emitted C carries no `__attribute__((packed))`, `sizeof` is 20 not 18, `width` sits at 14 not 12 | **REFUTED** — [FRICTION 127](FRICTION.md) |
| 1 | `size_of$` refuses every type `align(N)` applies to, so `align(N)` is unobservable from inside the language | **OPEN** |
| 3 | none of the four features appears anywhere in `docs/reference/`, the layer `docs/README.md` calls "the single answer layer" | **OPEN** |
| 4 | three of the four have zero users in `examples/` and `corelib/`; only `packed` has any (4) | **OPEN** |
| 6 | `vector[N]T` has no lane-wise min/max, so the clamp — the operation a grading kernel does as often as the multiply — drops out of the vector unit | **OPEN** |
| 8a | §5.5 omits `u8` from the ordered types, the mixed-type diagnostic omits `char`/`u32`/`u64`/`f32`, and the compiler's real rule is in neither | **OPEN** |
| 8b | `str()` on a vector, and a vector as a struct field, both work and are undocumented | **OPEN** |
| 8c | `bytes` `+` and `[a:b]` are in the spec and not in `reference/types.md`'s `bytes` section — correct docs, wrong layer | **OPEN** |
| 8d | two `-Wunused-value` warnings out of `core:strings` in every build | **OPEN** — the `handle` probe hit the same two |

Re-checked against `main` on 2026-09-22: 1, 3 and 6 reproduce exactly, including
the error text `'math__clamp' instantiated with T = vector[4]f32, which does not
satisfy comparable(T)` and `size_of$(C): only a packed struct has a stated byte
size`.

## Finding 2 is the important one, because it is wrong

It was reported as the finding "I would most want a Tycho maintainer to see": a
normative spec claim (§17.1a, "no padding between fields … each field sits at the
sum of the sizes before it") contradicted by the compiler's own output, with an
FFI consequence attached.

It does not reproduce. `tychoc --emit-c` on the probe's *own* `tgagrade/main.ty`,
run here, ends that struct with `} __attribute__((packed));`, and `cc` measuring
that definition verbatim reports:

```text
emitted-verbatim TgaHeader: sizeof=18 alignof=1 offsetof width=12 height=14 descriptor=17
```

— which is §17.1a exactly, and exactly the numbers the report called "byte-exact
would be". The attribute has been emitted since `packed` shipped (`5fff6d5c`).

**The defect is in the probe's own apparatus.** `probe/layout.c`, the file whose
header says the definitions were "copied VERBATIM out of `tychoc --emit-c`", has
the attribute on its `align(8)` struct and **not** on the packed one. The copy
dropped one line, `cc` faithfully measured the mis-copy, and the measurement was
then read back as a language defect. Every downstream sentence — 20 bytes,
`width` at 14, "what `packed` actually buys is the side-table", the §14 FFI
worry — is a description of `probe/layout.c`.

[probe-simd-2026-09-07.md](probe-simd-2026-09-07.md) closed with the lesson that
a report's own citations have to be opened rather than trusted. This is the
stronger form: **a report's own measurements have to be re-run.** A probe that
builds an independent instrument can be wrong about the instrument, and it will
present that error in exactly the format that makes it most convincing — a
number, from a second toolchain, against a quoted spec line.

## What went right

The diagnostics again, in the probe's words "the best part of the system": every
rejection named the rule, the value and usually the fix. `packed` and `align(N)`
together are refused by name; `vector[4]u8` is refused with the element rule
stated; a `math.clamp` on a vector names the constraint it failed. The four
features themselves all did what the spec said they do — `align(8)` measurably
raised size from 12 to 16, the swizzle lowered as read-both-then-write-both, and
262,144 graded pixels matched an independent float64 recomputation.

`corelib/raster/raster.ty`'s `BmpHeader` was singled out: "taught me the whole
file-format idiom in one read." It is the only in-tree teaching example any of
these four features has.

## What it could not do

- Observe `align(N)` from inside Tycho at all (finding 1). It left the language
  and ran `cc`.
- Confirm the little-endian-on-every-host guarantee — both available machines are
  little-endian, so the interesting half of §17.1b is still untested here.
- Keep the clamp in the vector unit (finding 6).
- Find any of the four features from the documentation's own recommended reading
  path (finding 3): "Without the section numbers in the brief I would have
  concluded they do not exist."

## Filing

The program is kept out of tree, and the afternoon's other one is not:
`tydu` came in as [`tools/tycho-du/`](../../tools/tycho-du) because it closed a
named gap (a `handle` built with `--shim`, which nothing demonstrated).
`tgagrade` closes no gap that way — its value was the harness, not the program.

But finding 4 is the standing argument on the other side: `vector[N]T`,
`align(N)` and swizzling have **no worked user anywhere in the tree**, and a
probe cannot close that by being thrown away. Whatever eventually does close it
will need a program, and this record is what the next one should be aimed past.
