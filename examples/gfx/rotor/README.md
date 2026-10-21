# Rotor Run — third caller for `core:gfx3d`

A third-person drone game and the third real caller for the software 3D
renderer. It uses the `Mesh` and `Place` layer in `core:gfx3d` for a multi-part
drone model and a spinning rotor, with a follow camera rather than Vector
Vault's first-person camera or Orbit's gallery camera.

The renderer runs at 480×270 and scales through `core:gfx.draw_fb`.

## Build and play

The shared font atlas is one directory up, so run from this directory:

```text
../../../tychoc1 main.ty --pkg sdl3 -o /tmp/rotor
/tmp/rotor
```

Controls:

```text
W / S          fly forward / back
A / D          turn
P              pause / resume
R              restart
Space          start / resume / restart after clearing
Escape         quit
```

Collect all 6 amber cells. The rotor and body are local meshes transformed by
`Place`; the rotor spins independently.

## Headless and checked lane

```text
SDL_VIDEODRIVER=dummy /tmp/rotor --headless --frames 60 \
    --snapshot /tmp/rotor.bmp

sh run.sh
```
