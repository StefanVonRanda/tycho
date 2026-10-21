# gfx — windowed Snake over SDL3

A validation program for Tycho's window, framebuffer and FFI path. The Snake
state and the RGBA framebuffer are Tycho values; the `core:gfx` shim only owns
the SDL window, the streaming texture,
key polling and presentation. The reusable layer lives in `core:gfx`; `pong/` is
the second caller that forces the shared API shape.

```text
# build the compiler once, at the repo root
make
cd examples/gfx

# build and play
../../tychoc1 main.ty --pkg sdl3 -o /tmp/tycho-snake
/tmp/tycho-snake

# deterministic headless run + BMP snapshot
SDL_VIDEODRIVER=dummy /tmp/tycho-snake --headless --frames 60 \
    --snapshot /tmp/snake.bmp

# the checked lane: headless BMP + Xvfb/X11 window
sh run.sh

# package a runnable directory
sh package.sh
cd dist && ./tycho-snake
```

Controls: **Space** start, **1/2/3** speed, **P** pause/resume, **R** restart,
**WASD/arrows** steer, **q/Esc** quit. The window is resizable and letterboxed;
the high score is stored in `snake.highscore` in the working directory (the
packaged `dist` directory when you run the package).

On macOS: `brew install sdl3 pkgconf`, then use the same build/package commands.

`--frames N` stops after N frames; `--headless` defaults to 240 frames and
skips the 16 ms frame sleep. A `--snapshot PATH` writes a 32-bit BMP through
`core:raster` at exit.

## What this proves

- Tycho can own a full RGBA framebuffer (`[int]`), draw a frame, and hand
  it to C as a pointer + length with the existing FFI.
- SDL3 opens a real window and uses its OpenGL renderer; the shim reports
  `video=... renderer=...` on startup. On this box the Weston/GL path reports
  `AMD Radeon 8060S Graphics (radeonsi, strix_halo, ACO)`.
- Input polling, title updates, frame pacing (via `core:time`) and a crash-free
  shutdown all work with the current FFI.
- The present path uses **real SDL struct layouts**: `SDL_FRect` by value for
  the destination, `SDL_Rect` by value for clips, and `[FVec]`
  (`SDL_FPoint`-layout) arrays for GPU lines. The canvas is letterboxed through
  a pure `View` value on resize.
- POD structs cross by value, return, and `inout`; `[POD]` arrays cross as
  pointer + length. Non-POD/packed structs stay rejected.
- Input is a tagged POD `Event` returned by the shim. `SDL_Event`'s union stays
  in C; Tycho sees only `kind`, `key`, `x`, `y` and `button`.
- Audio is a **stream**: Tycho generates signed-16-bit PCM into `bytes` and
  queues it with `gfx_audio_queue`; no callback crosses the boundary. The test
  lane sets `SDL_AUDIODRIVER=dummy`, so it is silent and deterministic.
- `assets/manifest.json` declares the texture set. Tycho parses it with
  `core:json`, loads each PNG through `core:image`, and fills a
  `[string: int]` cache of texture IDs. One scoped `TexSet` **handle** owns every
  SDL texture; `close(texset)` destroys them together before the renderer is
  torn down. The apple is a four-frame sheet, selected through the value-only
  `Anim`/`anim_frame` path and drawn over the food cell.
- `assets/font.png` is a 16×6 glyph atlas (32-px cells). It is decoded by
  `core:image`, uploaded as an SDL texture, and each glyph is drawn with its own
  source/destination `FRect`: menu, score, pause and terminal-state text all
  use it. No SDL_ttf is involved.
- The finished loop has a title menu, three speeds, pause/resume, restart,
  high-score persistence, fixed-timestep simulation, and resize letterboxing.

## FFI gaps this shim exposes

These are the next things to remove from the shim, in order:

1. Unions — C unions remain non-crossable by design; use a tagged POD like
   `Event`. A language-level union with initialization and ownership rules is a
   separate design, not a quick FFI flag.
2. Callbacks/function pointers — still unavailable; stream audio avoids them,
   but libraries that require callbacks need a C-side adapter.
3. Owned resources in collections — typed handles cannot live in arrays/maps.
   A single collection handle (`TexSet`) sidesteps that for a homogeneous set,
   but a general owned container is still future work.
4. Text/font and asset pipelines — the PNG glyph atlas and JSON manifest cover
   this demo; scalable vector text and richer asset metadata are still future
   work.

The shared layer is `core:gfx`; `pong/` is the second caller that forced its
API shape. The example remains the executable demonstration, not the library.
