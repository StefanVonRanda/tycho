# GPU probe — first `core:gfxgpu` frame

The first real SDL_GPU path: a depth-tested, shader-driven triangle presented
through a GPU swapchain while `core:gfx` still owns the window and input. It is
a renderer probe, not yet the full game port.

## Build the shader translator once

`core:gfxgpu` uses SDL_shadercross to translate committed SPIR-V to Vulkan,
Metal or D3D12 at runtime. It is not a system package on this machine:

```text
sh scripts/build-shadercross.sh
```

The script writes `build/shadercross/env.sh` with the build and runtime paths.
Source it from the repository root:

```text
. build/shadercross/env.sh
```

On macOS that sets `DYLD_LIBRARY_PATH`; on Linux it sets `LD_LIBRARY_PATH`
(and `PKG_CONFIG_PATH` on both). If you built the probe before this path was
wired, either rebuild it after sourcing `env.sh`, or add the dylib directory
manually:

```sh
export DYLD_LIBRARY_PATH="$PWD/build/shadercross/lib:$DYLD_LIBRARY_PATH"
```

Use `find build/shadercross -name 'libSDL3_shadercross*.dylib'` if the prefix
has a different layout.

## Build and run

```text
. build/shadercross/env.sh
cd examples/gfx/gpu
../../../tychoc1 main.ty --pkg sdl3 -o /tmp/gpuprobe
/tmp/gpuprobe
```

`run.sh` also adds an rpath and sets both loader variables, so its temporary
binary is directly runnable. On macOS the no-Xvfb branch opens the real display
instead of skipping.

Or run the checked lane:

```text
sh run.sh
```

The probe needs a real GPU backend: X11/Vulkan on Linux, Metal on macOS. It has
no meaningful dummy-video fallback; a headless CI host without a GPU backend is
a named skip, not a silent pass.

## What it proves

- SDL_GPU device, swapchain, depth texture, shader pipeline and uniform upload
- SPIR-V shader translation through SDL_shadercross
- coexistence with `core:gfx`'s window/input/audio path
- a real draw submitted to the GPU

The next step is the `core:gfxgpu` mesh/HUD API and the Rotor Run port.
