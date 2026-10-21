# Vector Vault — software 3D over `core:gfx`

A first-person collectathon built to answer one question: **can `core:gfx`
carry a real 3D workload without a new GPU/3D escape hatch?** The scene is a
z-buffered software renderer written in Tycho; `core:gfx` supplies the window,
events, audio, HUD text, resize transform and presentation.

The renderer runs at 480×270. The shared software camera/rasterizer lives in
`core:gfx3d`; with a GPU device, Vector Vault uses `core:gfxgpu` directly too:
world-space meshes, a camera MVP uniform, a depth target and the GPU HUD overlay.
Without a GPU it uses the software path and `core:gfx` presentation.

## Build and play

The font atlas is in `assets/`, so run from this directory. The GPU path needs
the SDL_shadercross runtime; build it with `scripts/build-shadercross.sh` and
source `build/shadercross/env.sh` first (see `../gpu/README.md`):

```text
. build/shadercross/env.sh
../../../tychoc1 main.ty --pkg sdl3 -o /tmp/vault
/tmp/vault

# or package a runnable directory
sh package.sh
cd dist && ./vector-vault
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

`run.sh` builds the example, checks the BMP header under the dummy driver, and
runs the same program in an Xvfb/X11 window when Xvfb is available. `corelib/test/gfx3d`
pins the depth mapping and near-plane clip numerically; `examples/gfx/rotor/run.sh`
checks the third caller and the GPU path.

## What this adds to the gfx path

- `core:gfx3d` owns the perspective camera, near-plane clipping, triangle
  rasterization, corrected barycentric inverse-depth interpolation and the
  flat-shaded box/quad primitives. It is pure Tycho: no SDL, handles or
  pointers.
- `core:gfx.draw_fb` accepts a source framebuffer smaller than the window; the
  shim caches a streaming texture at that source size and scales it into the
  destination `FRect`.
- Key-up events (`Event.kind == 6`) make held movement possible without polling
  SDL from Tycho or adding a callback.
- The existing `View`, font atlas, audio stream and resource handles are used
  unchanged; no public `ptr`, callback or union is introduced.

The second caller is `examples/gfx/orbit`: a gallery with an orbit camera. The
third is `examples/gfx/rotor`: a third-person drone example using `Mesh`/`Place`.
Together they justified extracting the shared renderer and mesh layer.
Flat-shaded triangles are the current library contract; textures, lighting and a
GPU backend remain out of scope until another caller needs them.
