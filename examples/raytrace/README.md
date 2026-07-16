# raytrace

A small ray tracer — a handful of spheres with diffuse + specular shading and one
reflection bounce — rendered to a QOI image via `core:raster`. About 150 lines of
pure Tycho, no FFI.

It exists as a dogfood for a corner the benchmarks don't reach: **float-heavy
struct value semantics**. Every `Vec3` operation (`v_add`, `v_scale`, `v_dot`,
`v_norm`, `v_reflect`, …) returns a *fresh* struct by value, and those values are
copied through deep call chains and recursion (the reflection bounce). The whole
render — hundreds of thousands of transient `Vec3`s — is carried by the implicit
per-scope arena with zero manual memory management, and it's ASan/leak-clean.

Because the scene and camera are fixed, the render is deterministic: the C
compiler (`tychoc`) and the self-hosted `tychoc0` must produce a **byte-identical**
QOI, which the harness checks (a real-program float differential), along with the
QOI file header and a golden summary line.

```sh
make raytrace                 # build (both compilers + ASan), render, diff, check
sh examples/raytrace/run.sh   # same, standalone
```

Run the binary directly to keep the image:

```sh
./tychoc examples/raytrace/main.ty -o /tmp/rt && (cd /tmp && ./rt)   # writes out.qoi
```
