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

Then set the paths printed by the script. From the repository root:

```sh
export PKG_CONFIG_PATH="$PWD/build/shadercross/lib64/pkgconfig:$PWD/build/shadercross/lib/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$PWD/build/shadercross/lib64:$PWD/build/shadercross/lib:$LD_LIBRARY_PATH"
```

On macOS, use `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`.

## Build and run

```text
../../../tychoc1 main.ty --pkg sdl3 -o /tmp/gpuprobe
/tmp/gpuprobe
```

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
