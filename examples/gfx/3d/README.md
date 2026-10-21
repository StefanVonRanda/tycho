# Vector Vault — software 3D over `core:gfx`

A first-person collectathon built to answer one question: **can `core:gfx`
carry a real 3D game without a new GPU/3D escape hatch?** The scene is a
z-buffered software renderer written in Tycho; `core:gfx` supplies the window,
events, audio, HUD text, resize transform and presentation.

The renderer runs at 480×270 and `draw_fb` scales that texture into the
window. Walls, pillars and cells are flat-shaded triangles with a depth buffer;
`View` keeps the framebuffer letterboxed on resize. The 3D math and scene remain
example-local: only the source-framebuffer scaling was added to `core:gfx`.

## Build and play

Assets (the shared font atlas) are one directory up, so run from this directory:

```text
../../../tychoc1 main.ty --pkg sdl3 -o /tmp/vault
/tmp/vault
```

Controls:

```text
W / S          move forward / back
A / D          strafe
mouse          look
Z / C          turn left / right (keyboard fallback)
P              pause / resume
R              restart
Space          start / resume / restart after clearing
Escape         quit
```

Collect all 8 amber cells, then reach the green exit in the far wall. The timer
starts when the run starts.

## Headless and checked lane

```text
SDL_VIDEODRIVER=dummy /tmp/vault --headless --frames 60 \
    --snapshot /tmp/vault.bmp

sh run.sh
```

`run.sh` builds the game, checks the BMP header under the dummy driver, and runs
the same program in an Xvfb/X11 window when Xvfb is available.

## What this adds to the gfx path

- A perspective camera, near-plane rejection, triangle rasterization,
  barycentric depth interpolation and a full-frame z-buffer, all in Tycho.
- `draw_fb` now accepts a source framebuffer smaller than the window; the shim
  caches a streaming texture at that source size and scales it into the
  destination `FRect`.
- Key-up events (`Event.kind == 6`) make held movement possible without polling
  SDL from Tycho or adding a callback.
- The existing `View`, font atlas, audio stream and resource handles are used
  unchanged; no public `ptr`, callback or union is introduced.

This is deliberately a software renderer, not a claim that `core:gfx` has a GPU
3D API. If a second 3D caller arrives, the renderer/mesh layer is the point to
extract; one game is not enough evidence for that API.
