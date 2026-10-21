<!--
Draft release notes. Edit this before publishing, then:
  make ci
  make release-check                    # builds twice; smoke-tests and compares the native tarballs
  scripts/release.sh <version> --mingw  # builds and smoke-tests the Windows tarball
  gh release create <version> dist/tycho-<version>-*.tar.gz dist/tycho-<version>-*.sha256 --notes-file RELEASE_NOTES.md
  gh release edit <version> --prerelease
Build one tarball per platform (there is no hosted CI); attach them all to the release.
Name the version in the globs: dist/ still holds the previous release's tarballs.
-->

Tycho 0.8.9 — pre-1.0, no stability guarantees (see the [README](README.md) for
what that means in practice). Prebuilt binaries are attached, so you can try the
language without building from source.

**0.8.9 is the 3D and first-GPU checkpoint.** The value model and the
O1–O7/FFI work from 0.8.8 are unchanged; this release adds a pure software-3D
library, three 3D callers, and an explicitly experimental SDL_GPU backend.

## Software 3D

- **`core:gfx3d`** is a pure Tycho camera and flat-shaded triangle rasterizer.
  It exposes `Vec3`, `Camera`, `Target`, `SVert`, `Tri`, `Mesh` and `Place`;
  `look_at`, `project`, `draw_tri`, `draw_quad`, `draw_box`, `clear`, `mesh`,
  `add_tri`, `add_quad`, `add_box` and `draw_mesh`.
- Near-plane clipping and corrected barycentric inverse-depth interpolation are
  pinned by `corelib/test/gfx3d`, including the depth-ordering case that broke
  the first cube renderer.
- `Mesh` holds local-space triangles; `Place` applies a caller-built
  right/up/forward basis, origin and uniform scale without copying the mesh.

## Three callers

- **Vector Vault** — first-person collectathon, z-buffered triangles, portal,
  timer, pause/restart, resize and a standalone `dist/` package.
- **Orbital Gallery** — orbit camera and gallery scene, exercising a different
  camera model.
- **Rotor Run** — third-person drone game using `Mesh`/`Place`, a multi-part
  body and independently spinning rotor, follow camera and pickups.

All three use the same `core:gfx` window/input/audio/font path, and the software
renderer remains available.

## GPU backend — experimental

- **`core:gfxgpu`** creates an SDL_GPU device, pipeline, depth target and
  swapchain for a `core:gfx` window.
- Shaders are committed SPIR-V and translated to the native format by
  **SDL_shadercross** (`scripts/build-shadercross.sh` builds the pinned runtime).
- Vector Vault and Rotor Run upload world-space meshes, use a camera MVP vertex
  uniform, draw depth-tested scenes, and blend font-atlas HUD overlays directly
  on the swapchain. The same code has run on Linux/Vulkan and macOS/Metal.
- The backend is deliberately marked **experimental**: the shadercross runtime
  is a separate build dependency, Windows GPU validation is not complete, and
  at 480×270 internal resolution it is not yet faster than the software path.
  Software remains the fallback.

## Compatibility

No new value-model or escape-hatch language change. `surface.lock` moved by
addition only. The `strings.parse_int`, `eprintln`, `path.real` and FFI changes
from 0.8.8 remain in force.

## Verification

- `make corelib` — 49 ok.
- `make parse-check` — 1528 files.
- `make surface-check` — 486 corelib functions.
- Three software 3D callers and the GPU probe pass headless/X11 checks.
- Valgrind and ASan are clean on the software callers and the gfx/image smoke
  tests. GPU-driver leak coverage is not claimed on Linux.
- The full local CI and release archive checks are run before tagging.

0.8.9 is pre-1.0 and there are **no stability guarantees**: anything here may
change before 1.0. See [CHANGELOG.md](CHANGELOG.md) for the accumulating record.
