#include <SDL3/SDL.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    size_t pixels_cap;
    int w, h;
    int tex_w, tex_h;
} Gfx;

typedef struct {
    SDL_AudioStream *stream;
} GfxAudio;

void *gfx_audio_open(int64_t rate, int64_t channels) {
    if (rate <= 0 || rate > 192000 || channels <= 0 || channels > 8) return NULL;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "gfx: SDL audio init: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_AudioSpec spec = { SDL_AUDIO_S16LE, (int)channels, (int)rate };
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        fprintf(stderr, "gfx: SDL_OpenAudioDeviceStream: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        fprintf(stderr, "gfx: SDL_ResumeAudioStreamDevice: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    GfxAudio *a = (GfxAudio *)calloc(1, sizeof(GfxAudio));
    if (!a) {
        SDL_DestroyAudioStream(stream);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return NULL;
    }
    a->stream = stream;
    fprintf(stderr, "gfx: audio=%s rate=%d channels=%d\n",
            SDL_GetCurrentAudioDriver(), (int)rate, (int)channels);
    return a;
}

int64_t gfx_audio_queue(void *handle, const unsigned char *pcm, int64_t len) {
    GfxAudio *a = (GfxAudio *)handle;
    if (!a || !a->stream || !pcm || len < 0 || len > INT_MAX) return -1;
    if (!SDL_PutAudioStreamData(a->stream, pcm, (int)len)) return -2;
    return 0;
}

int64_t gfx_audio_close(void *handle) {
    GfxAudio *a = (GfxAudio *)handle;
    if (!a) return 0;
    SDL_DestroyAudioStream(a->stream);
    free(a);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return 0;
}

typedef struct {
    Gfx *g;
    SDL_Texture **items;
    int n, cap;
} TexSet;

void *gfx_texset_open(void *handle) {
    Gfx *g = (Gfx *)handle;
    if (!g) return NULL;
    TexSet *set = (TexSet *)calloc(1, sizeof(TexSet));
    if (!set) return NULL;
    set->g = g;
    return set;
}

int64_t gfx_texset_create(void *handle, const unsigned char *rgba, int64_t len,
                          int64_t w, int64_t h) {
    TexSet *set = (TexSet *)handle;
    if (!set || !set->g || !rgba || w <= 0 || h <= 0 || w > 16384 || h > 16384) return -1;
    if (len < w * h * 4 || len > INT_MAX) return -1;
    if (set->n == set->cap) {
        int nc = set->cap ? set->cap * 2 : 8;
        SDL_Texture **ni = (SDL_Texture **)realloc(set->items, (size_t)nc * sizeof(*ni));
        if (!ni) return -1;
        set->items = ni;
        set->cap = nc;
    }
    SDL_Texture *tex = SDL_CreateTexture(set->g->renderer, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, (int)w, (int)h);
    if (!tex) {
        fprintf(stderr, "gfx: SDL_CreateTexture(sprite): %s\n", SDL_GetError());
        return -1;
    }
    if (!SDL_UpdateTexture(tex, NULL, rgba, (int)(w * 4))) {
        fprintf(stderr, "gfx: SDL_UpdateTexture(sprite): %s\n", SDL_GetError());
        SDL_DestroyTexture(tex);
        return -1;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    set->items[set->n] = tex;
    return set->n++;
}

int64_t gfx_texset_draw(void *handle, int64_t id, SDL_FRect src, SDL_FRect dst,
                        int64_t r, int64_t g8, int64_t b, int64_t a) {
    TexSet *set = (TexSet *)handle;
    if (!set || id < 0 || id >= set->n) return -1;
    if (r < 0 || r > 255 || g8 < 0 || g8 > 255 || b < 0 || b > 255 || a < 0 || a > 255)
        return -2;
    SDL_Texture *tex = set->items[id];
    SDL_SetTextureColorMod(tex, (Uint8)r, (Uint8)g8, (Uint8)b);
    SDL_SetTextureAlphaMod(tex, (Uint8)a);
    if (!SDL_RenderTexture(set->g->renderer, tex, &src, &dst)) return -3;
    return 0;
}

int64_t gfx_texset_close(void *handle) {
    TexSet *set = (TexSet *)handle;
    if (!set) return 0;
    for (int i = 0; i < set->n; i++) SDL_DestroyTexture(set->items[i]);
    free(set->items);
    free(set);
    return 0;
}

void *gfx_open(int64_t w, int64_t h, const char *title) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return NULL;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "gfx: SDL_Init: %s\n", SDL_GetError());
        return NULL;
    }
    Gfx *g = (Gfx *)calloc(1, sizeof(Gfx));
    if (!g) {
        SDL_Quit();
        return NULL;
    }
    g->w = (int)w;
    g->h = (int)h;
    g->window = SDL_CreateWindow(title, g->w, g->h, 0);
    if (!g->window) {
        fprintf(stderr, "gfx: SDL_CreateWindow: %s\n", SDL_GetError());
        free(g);
        SDL_Quit();
        return NULL;
    }
    SDL_SetWindowResizable(g->window, true);
    g->renderer = SDL_CreateRenderer(g->window, NULL);
    if (!g->renderer) {
        fprintf(stderr, "gfx: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g->window);
        free(g);
        SDL_Quit();
        return NULL;
    }
    g->texture = SDL_CreateTexture(g->renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, g->w, g->h);
    if (!g->texture) {
        fprintf(stderr, "gfx: SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(g->renderer);
        SDL_DestroyWindow(g->window);
        free(g);
        SDL_Quit();
        return NULL;
    }
    SDL_SetTextureScaleMode(g->texture, SDL_SCALEMODE_NEAREST);
    g->tex_w = g->w;
    g->tex_h = g->h;
    g->pixels_cap = (size_t)g->w * (size_t)g->h;
    fprintf(stderr, "gfx: video=%s renderer=%s\n",
            SDL_GetCurrentVideoDriver(), SDL_GetRendererName(g->renderer));
    g->pixels = (uint32_t *)malloc((size_t)g->w * (size_t)g->h * sizeof(uint32_t));
    if (!g->pixels) {
        SDL_DestroyTexture(g->texture);
        SDL_DestroyRenderer(g->renderer);
        SDL_DestroyWindow(g->window);
        free(g);
        SDL_Quit();
        return NULL;
    }
    return g;
}

int64_t gfx_begin(void *handle) {
    Gfx *g = (Gfx *)handle;
    if (!g || !SDL_RenderClear(g->renderer)) return -1;
    return 0;
}

int64_t gfx_draw_fb(void *handle, const int64_t *fb, int64_t len, int64_t w, int64_t h,
                    SDL_FRect dst) {
    Gfx *g = (Gfx *)handle;
    if (!g || !fb || w <= 0 || h <= 0 || w > 16384 || h > 16384) return -1;
    int64_t n = w * h;
    if (len < n) return -1;

    /* The source may be a low-resolution software framebuffer scaled up by the
       renderer. Recreate only when its dimensions change, then keep the same
       streaming texture for every frame. */
    if (g->tex_w != (int)w || g->tex_h != (int)h) {
        SDL_Texture *nt = SDL_CreateTexture(g->renderer, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING, (int)w, (int)h);
        if (!nt) return -4;
        SDL_SetTextureScaleMode(nt, SDL_SCALEMODE_NEAREST);
        SDL_DestroyTexture(g->texture);
        g->texture = nt;
        g->tex_w = (int)w;
        g->tex_h = (int)h;
    }
    size_t need = (size_t)n;
    if (need > g->pixels_cap) {
        uint32_t *np = (uint32_t *)realloc(g->pixels, need * sizeof(uint32_t));
        if (!np) return -5;
        g->pixels = np;
        g->pixels_cap = need;
    }
    for (int64_t i = 0; i < n; i++) g->pixels[i] = (uint32_t)fb[i];
    if (!SDL_UpdateTexture(g->texture, NULL, g->pixels, (int)(w * 4))) return -2;
    if (!SDL_RenderTexture(g->renderer, g->texture, NULL, &dst)) return -3;
    return 0;
}

int64_t gfx_draw_lines(void *handle, SDL_Rect clip, const SDL_FPoint *pts, int64_t npts,
                       int64_t r, int64_t g8, int64_t b, int64_t a) {
    Gfx *g = (Gfx *)handle;
    if (!g || !pts || npts < 2) return -1;
    if (r < 0 || r > 255 || g8 < 0 || g8 > 255 || b < 0 || b > 255 || a < 0 || a > 255)
        return -2;
    if (!SDL_SetRenderClipRect(g->renderer, &clip)) return -3;
    SDL_SetRenderDrawColor(g->renderer, (Uint8)r, (Uint8)g8, (Uint8)b, (Uint8)a);
    if (!SDL_RenderLines(g->renderer, pts, (int)npts)) {
        SDL_SetRenderClipRect(g->renderer, NULL);
        return -4;
    }
    SDL_SetRenderClipRect(g->renderer, NULL);
    return 0;
}

int64_t gfx_present(void *handle) {
    Gfx *g = (Gfx *)handle;
    if (!g || !SDL_RenderPresent(g->renderer)) return -1;
    return 0;
}

typedef struct {
    int32_t kind;       /* 0 none, 1 quit, 2 key down, 3 mouse move, 4 mouse button, 5 resize, 6 key up */
    int32_t key;        /* 1..4 direction, 27 quit, otherwise the SDL keycode */
    int32_t x, y;       /* mouse coordinates for kinds 3/4 */
    int32_t button;     /* SDL mouse button for kind 4 */
} Event;

static int32_t event_key(SDL_Keycode k) {
    if (k == SDLK_ESCAPE || k == SDLK_Q) return 27;
    if (k == SDLK_W || k == SDLK_UP) return 1;
    if (k == SDLK_D || k == SDLK_RIGHT) return 2;
    if (k == SDLK_S || k == SDLK_DOWN) return 3;
    if (k == SDLK_A || k == SDLK_LEFT) return 4;
    return (int32_t)k;
}

Event gfx_poll_event(void *handle) {
    (void)handle;
    Event out = {0, 0, 0, 0, 0};
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            out.kind = 1;
            return out;
        }
        if (e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            out.kind = 5;
            out.x = (int32_t)e.window.data1;
            out.y = (int32_t)e.window.data2;
            return out;
        }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            out.kind = 2;
            out.key = event_key(e.key.key);
            return out;
        }
        if (e.type == SDL_EVENT_KEY_UP) {
            out.kind = 6;
            out.key = event_key(e.key.key);
            return out;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            out.kind = 3;
            out.x = (int32_t)e.motion.x;
            out.y = (int32_t)e.motion.y;
            return out;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            out.kind = 4;
            out.x = (int32_t)e.button.x;
            out.y = (int32_t)e.button.y;
            out.button = (int32_t)e.button.button;
            return out;
        }
    }
    return out;
}

int64_t gfx_window_width(void *handle) {
    Gfx *g = (Gfx *)handle;
    int w = 0, h = 0;
    if (!g || !SDL_GetRenderOutputSize(g->renderer, &w, &h)) return g ? g->w : 0;
    return (int64_t)w;
}

int64_t gfx_window_height(void *handle) {
    Gfx *g = (Gfx *)handle;
    int w = 0, h = 0;
    if (!g || !SDL_GetRenderOutputSize(g->renderer, &w, &h)) return g ? g->h : 0;
    return (int64_t)h;
}

/* Internal C-to-C accessor for renderer packages. It is deliberately not
   exposed in gfx.ty: Tycho never sees the SDL_Window pointer. */
void *gfx_native_window(void *handle) {
    Gfx *g = (Gfx *)handle;
    return g ? g->window : NULL;
}

int64_t gfx_set_title(void *handle, const char *title) {
    Gfx *g = (Gfx *)handle;
    if (!g) return -1;
    SDL_SetWindowTitle(g->window, title);
    return 0;
}

int64_t gfx_close(void *handle) {
    Gfx *g = (Gfx *)handle;
    if (!g) return 0;
    free(g->pixels);
    SDL_DestroyTexture(g->texture);
    SDL_DestroyRenderer(g->renderer);
    SDL_DestroyWindow(g->window);
    free(g);
    SDL_Quit();
    return 0;
}
