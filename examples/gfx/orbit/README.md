# Orbital Gallery — second caller for `core:gfx3d`

A deliberately different 3D caller from Vector Vault. It uses the extracted
`core:gfx3d` camera/rasterizer with an orbit camera, a gallery scene and an
independently moving satellite. Vector Vault uses the same package from a
first-person camera; this second caller is what justified extracting it.

The renderer still runs at 480×270 and scales through `core:gfx.draw_fb`.

## Build and play

The shared font atlas is one directory up, so run from this directory:

```text
../../../tychoc1 main.ty --pkg sdl3 -o /tmp/orbit
/tmp/orbit
```

Controls:

```text
Z / C          orbit left / right
W / S          zoom in / out
P              pause / resume auto-orbit
R              reset view
Escape         quit
```

The satellite keeps moving around the plinth; the gallery itself is static.

## Headless and checked lane

```text
SDL_VIDEODRIVER=dummy /tmp/orbit --headless --frames 60 \
    --snapshot /tmp/orbit.bmp

sh run.sh
```

`run.sh` checks the headless BMP and, when Xvfb is available, the same program
in an X11 window.
