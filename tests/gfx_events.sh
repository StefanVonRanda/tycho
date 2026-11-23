#!/bin/sh
# core:gfx event mapping (FRICTION 212): SDL events pushed in, Event fields
# asserted. Compiles the shim directly against SDL's dummy video driver, so it
# needs SDL3 but no display. Skips (exit 0, says so) when SDL3 is absent.
set -eu
cd "$(dirname "$0")/.."
if ! pkg-config --exists sdl3 2>/dev/null; then
    echo "skip gfx_events (no sdl3)"
    exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cat >"$tmp/h.c" <<'C'
#include "corelib/gfx/gfx_shim.c"
static int fails = 0;
static void push(SDL_Event e) { SDL_PushEvent(&e); }
/* A pushed event can surface a poll or two late, so wait for it (bounded). */
static Event next(void) {
    for (int i = 0; i < 200; i++) {
        Event v = gfx_poll_event(NULL);
        if (v.kind != 0) return v;
        SDL_Delay(1);
    }
    Event none = {0};
    return none;
}
static void want(const char *what, int got, int exp) {
    if (got != exp) { printf("FAIL %s: got %d, want %d\n", what, got, exp); fails++; }
}
int main(void) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { printf("SDL_Init: %s\n", SDL_GetError()); return 2; }
    SDL_Event e;
    SDL_zero(e); e.type = SDL_EVENT_KEY_DOWN; e.key.key = SDLK_W; push(e);
    Event v = next();
    want("W down kind", v.kind, 2); want("W folded key", v.key, 1); want("W sym", v.sym, (int)SDLK_W);
    SDL_zero(e); e.type = SDL_EVENT_KEY_UP; e.key.key = SDLK_UP; push(e);
    v = next();
    want("Up up kind", v.kind, 6); want("Up folded key", v.key, 1); want("Up sym", v.sym, (int)SDLK_UP);
    SDL_zero(e); e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = 1; e.button.x = 10; e.button.y = 20; push(e);
    v = next();
    want("press kind", v.kind, 4); want("press button", v.button, 1); want("press x", v.x, 10);
    SDL_zero(e); e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.button = 1; push(e);
    v = next();
    want("release kind", v.kind, 7); want("release button", v.button, 1);
    SDL_zero(e); e.type = SDL_EVENT_WINDOW_RESIZED; e.window.data1 = 640; e.window.data2 = 400; push(e);
    v = next();
    want("logical kind", v.kind, 5); want("logical w", v.x, 640);
    SDL_zero(e); e.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED; e.window.data1 = 1280; e.window.data2 = 800; push(e);
    v = next();
    want("pixel kind", v.kind, 8); want("pixel w", v.x, 1280); want("pixel h", v.y, 800);
    SDL_zero(e); e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = 2.0f; e.wheel.integer_y = 2;
    e.wheel.direction = SDL_MOUSEWHEEL_NORMAL; push(e);
    v = next();
    want("wheel kind", v.kind, 9); want("wheel y", v.y, 2);
    SDL_zero(e); e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = 1.0f; e.wheel.integer_y = 1;
    e.wheel.direction = SDL_MOUSEWHEEL_FLIPPED; push(e);
    v = next();
    want("flipped wheel y", v.y, -1);
    SDL_Quit();
    if (fails) return 1;
    printf("ok gfx_events\n");
    return 0;
}
C
${CC:-cc} -std=c11 -I. $(pkg-config --cflags sdl3) "$tmp/h.c" -o "$tmp/h" $(pkg-config --libs sdl3) -lm
SDL_VIDEODRIVER=dummy "$tmp/h"
