# `core:gfx` — value semantics at the SDL3 boundary

`core:gfx` is an optional SDL3-backed window, 2D renderer, event and stream-audio
layer. It is a C-shim package, so the question it has to answer is not "can SDL
do it" but **what is allowed to cross into Tycho**.

## The rule

The public surface exposes **values and affine handles only**:

- values: `Event`, `FRect`, `IRect`, `FVec`, `AssetSpec`, `string`, `bytes`,
  `[int]`, `[FVec]`
- owned resources: `Window`, `Audio`, `TexSet` handles
- integers: texture IDs scoped to a `TexSet`

There is **no `ptr` in any public `gfx` signature**, no callback parameter, and
no union. The SDL event union and every C pointer stay inside `gfx_shim.c`.

A future addition that wants a raw pointer, a callback or a union has to argue
against this rule, not around it.

## Ownership

- `Window` and `Audio` are ordinary affine handles: one owner, no copy, no
  storage in arrays/maps/structs, freed at scope exit.
- `TexSet` is the collection handle. It owns every texture created through it;
  the Tycho-side cache stores plain `int` IDs. `close(texset)` destroys the set
  before the renderer is torn down.
- A texture ID is never a pointer. Passing an ID to a different `TexSet` is a
  logic error (the shim range-checks it); it is not a memory-safety escape.
- `load_texture` copies decoded pixels into an SDL texture and keeps no Tycho
  buffer. `audio_queue` copies PCM into the SDL stream. `[int]`, `bytes`,
  `string` and `[FVec]` arguments are borrowed only for the duration of a call.

## Threads

Handles are not shared across tasks. The window, renderer, event pump and audio
stream are main-thread objects. `core:gfx` does not add a global resource
registry, a background renderer or a Tycho callback thread.

## What would need language work

These are deliberately outside the current package:

- general `[handle]` / `owned [T]` containers;
- raw C unions;
- direct function-pointer/callback FFI with closure and lifetime rules.

The collection-handle pattern covers the homogeneous resource set without any of
them. If a real third caller makes the pattern insufficient, the language
decision should be made then, with the evidence in hand.
