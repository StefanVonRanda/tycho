# Pong — second caller for the gfx engine

Pong imports the shared `core:gfx` package and its auto-linked shim, the same
ones Snake uses. It exists to force the common API shape: window, framebuffer, events,
audio, texture set, font atlas and presentation. Its ball is a four-frame sprite
sheet drawn through the same `Anim`/`draw_anim` path as Snake's apple.

```text
W / Up      move the left paddle up
S / Down    move the left paddle down
q / Escape  quit
```

Build and play:

```text
../../../tychoc1 main.ty --pkg sdl3 -o /tmp/tycho-pong
/tmp/tycho-pong
```

Headless test:

```text
sh run.sh
```

`--frames N` and `--snapshot PATH` work as in the Snake example. The test lane
uses `SDL_AUDIODRIVER=dummy` and a PID-derived Xvfb display.

## Shared boundary

The `core:gfx` package owns the handle types and wrappers:

- `gfx.Window`, `gfx.Audio`, `gfx.TexSet`
- `gfx.Event`, `gfx.FRect`, `gfx.IRect`, `gfx.FVec`
- framebuffer/text helpers, manifest loading, audio beep and BMP snapshot

Each caller declares only the three handle-returning openers:

```text
extern fn gfx_open(w: int, h: int, title: string) -> gfx.Window
extern fn gfx_audio_open(rate: int, channels: int) -> gfx.Audio
extern fn gfx_texset_open(g: gfx.Window) -> gfx.TexSet
```

That is the package boundary `core:gfx` now exposes.
