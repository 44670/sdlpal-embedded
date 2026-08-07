#include "SDL.h"

#include <ctype.h>

#ifndef SHIM_MAX_SURFACES
#define SHIM_MAX_SURFACES 32
#endif
#ifndef SHIM_MAX_PALETTES
#define SHIM_MAX_PALETTES 16
#endif
#ifndef SHIM_MAX_TEXTURES
#define SHIM_MAX_TEXTURES 4
#endif
#ifndef SHIM_SURFACE_PIXELS
#define SHIM_SURFACE_PIXELS (320u * 240u * 4u)
#endif
#ifndef SHIM_TEXTURE_PIXELS
#define SHIM_TEXTURE_PIXELS (320u * 240u * 4u)
#endif
#ifndef SHIM_KEYBOARD_KEYS
#define SHIM_KEYBOARD_KEYS 512
#endif
#ifndef SHIM_EVENT_QUEUE
#define SHIM_EVENT_QUEUE 64
#endif

struct SDL_Window {
    int w;
    int h;
};

struct SDL_Renderer {
    int unused;
};

struct SDL_Texture {
    int used;
    int w;
    int h;
    int pitch;
#if !defined(PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY)
    Uint8 pixels[SHIM_TEXTURE_PIXELS];
#endif
};

typedef struct ShimSurfaceSlot {
    int used;
    int owns_pixels;
    SDL_Surface surface;
    SDL_PixelFormat format;
#if !defined(PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY)
    Uint8 pixels[SHIM_SURFACE_PIXELS];
#endif
} ShimSurfaceSlot;

typedef struct ShimPaletteSlot {
    int used;
    SDL_Palette palette;
    SDL_Color colors[256];
} ShimPaletteSlot;

static ShimSurfaceSlot shim_surfaces[SHIM_MAX_SURFACES];
static ShimPaletteSlot shim_palettes[SHIM_MAX_PALETTES];
static SDL_Window shim_window;
static SDL_Renderer shim_renderer;
static SDL_Texture shim_textures[SHIM_MAX_TEXTURES];
static SDL_Texture *shim_present_texture;
static Uint8 shim_keyboard[SHIM_KEYBOARD_KEYS];
static SDL_Event shim_events[SHIM_EVENT_QUEUE];
static unsigned shim_event_head;
static unsigned shim_event_tail;
#if !PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS
static Uint32 shim_ticks;
#endif
static char shim_error[160];

#if PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS
void PalEngineBridge_RenderPresent(const void *pixels, int pitch, int w, int h);
int PalEngineBridge_PollEvent(SDL_Event *event);
Uint32 PalEngineBridge_GetTicks(void);
void PalEngineBridge_Delay(Uint32 ms);
#else
void PalEngineBridge_RenderPresent(const void *pixels, int pitch, int w, int h) __attribute__((weak));
int PalEngineBridge_PollEvent(SDL_Event *event) __attribute__((weak));
Uint32 PalEngineBridge_GetTicks(void) __attribute__((weak));
void PalEngineBridge_Delay(Uint32 ms) __attribute__((weak));
#endif

static void apply_keyboard_event(const SDL_Event *event)
{
    int scancode;

    if (event == NULL || (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)) {
        return;
    }
    scancode = SDL_GetScancodeFromKey(event->key.keysym.sym);
    if (scancode > 0 && scancode < SHIM_KEYBOARD_KEYS) {
        shim_keyboard[scancode] = event->type == SDL_KEYDOWN ? 1 : 0;
    }
}

static int bytes_for_depth(int depth)
{
    return depth <= 8 ? 1 : depth <= 16 ? 2 : 4;
}

static ShimSurfaceSlot *find_surface_slot(SDL_Surface *surface)
{
    int i;
    for (i = 0; i < SHIM_MAX_SURFACES; i++) {
        if (shim_surfaces[i].used && &shim_surfaces[i].surface == surface) {
            return &shim_surfaces[i];
        }
    }
    return NULL;
}

static void init_format(SDL_PixelFormat *format, int depth, Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
    if (format == NULL) {
        return;
    }
    memset(format, 0, sizeof(*format));
    format->format = depth == 32 ? SDL_PIXELFORMAT_ARGB8888 : 0;
    format->BitsPerPixel = (Uint8)depth;
    format->BytesPerPixel = (Uint8)bytes_for_depth(depth);
    format->Rmask = rmask;
    format->Gmask = gmask;
    format->Bmask = bmask;
    format->Amask = amask;
}

static SDL_Surface *create_surface_common(Uint32 flags, int width, int height, int depth, int pitch,
                                          void *pixels, Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
    int i;
    size_t bytes;
    if (width <= 0 || height <= 0 || depth <= 0) {
        return NULL;
    }
    if (pitch <= 0) {
        pitch = width * bytes_for_depth(depth);
    }
    bytes = (size_t)pitch * (size_t)height;
    for (i = 0; i < SHIM_MAX_SURFACES; i++) {
        ShimSurfaceSlot *slot = &shim_surfaces[i];
        if (slot->used) {
            continue;
        }
#if defined(PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY)
        if (pixels == NULL) {
            return NULL;
        }
#else
        if (pixels == NULL && bytes > sizeof(slot->pixels)) {
            return NULL;
        }
#endif
        memset(slot, 0, sizeof(*slot));
        slot->used = 1;
        slot->owns_pixels = pixels == NULL;
        init_format(&slot->format, depth, rmask, gmask, bmask, amask);
        slot->surface.flags = flags | (pixels != NULL ? SDL_PREALLOC : 0);
        slot->surface.format = &slot->format;
        slot->surface.w = width;
        slot->surface.h = height;
        slot->surface.pitch = pitch;
        slot->surface.pixels = pixels;
#if !defined(PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY)
        if (pixels == NULL) {
            slot->surface.pixels = slot->pixels;
        }
        if (pixels == NULL) {
            memset(slot->pixels, 0, bytes);
        }
#endif
        return &slot->surface;
    }
    return NULL;
}

static Uint32 read_pixel(const SDL_Surface *surface, int x, int y)
{
    const Uint8 *p;
    if (surface == NULL || surface->pixels == NULL || x < 0 || y < 0 || x >= surface->w || y >= surface->h) {
        return 0;
    }
    p = (const Uint8 *)surface->pixels + (size_t)y * (size_t)surface->pitch +
        (size_t)x * (size_t)surface->format->BytesPerPixel;
    switch (surface->format->BytesPerPixel) {
    case 1:
        return p[0];
    case 2:
        return (Uint32)p[0] | ((Uint32)p[1] << 8);
    default:
        return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
    }
}

static void write_pixel(SDL_Surface *surface, int x, int y, Uint32 value)
{
    Uint8 *p;
    if (surface == NULL || surface->pixels == NULL || x < 0 || y < 0 || x >= surface->w || y >= surface->h) {
        return;
    }
    p = (Uint8 *)surface->pixels + (size_t)y * (size_t)surface->pitch +
        (size_t)x * (size_t)surface->format->BytesPerPixel;
    switch (surface->format->BytesPerPixel) {
    case 1:
        p[0] = (Uint8)value;
        break;
    case 2:
        p[0] = (Uint8)value;
        p[1] = (Uint8)(value >> 8);
        break;
    default:
        p[0] = (Uint8)value;
        p[1] = (Uint8)(value >> 8);
        p[2] = (Uint8)(value >> 16);
        p[3] = (Uint8)(value >> 24);
        break;
    }
}

static Uint32 convert_pixel(const SDL_Surface *src, const SDL_Surface *dst, Uint32 value)
{
    Uint8 r;
    Uint8 g;
    Uint8 b;
    if (src == NULL || dst == NULL || src->format == NULL || dst->format == NULL) {
        return value;
    }
    if (src->format->BytesPerPixel == dst->format->BytesPerPixel &&
        src->format->BytesPerPixel == 1) {
        return value;
    }
    SDL_GetRGB(value, src->format, &r, &g, &b);
    return SDL_MapRGB(dst->format, r, g, b);
}

int SDL_Init(Uint32 flags)
{
    (void)flags;
    return SDL_OK;
}

void SDL_Quit(void)
{
}

const char *SDL_GetError(void)
{
    return shim_error;
}

void SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(shim_error, sizeof(shim_error), fmt != NULL ? fmt : "", ap);
    va_end(ap);
}

Uint32 SDL_GetTicks(void)
{
#if PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS
    return PalEngineBridge_GetTicks();
#else
    if (PalEngineBridge_GetTicks != NULL) {
        return PalEngineBridge_GetTicks();
    }
    return shim_ticks;
#endif
}

void SDL_Delay(Uint32 ms)
{
#if PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS
    PalEngineBridge_Delay(ms);
#else
    if (PalEngineBridge_Delay != NULL) {
        PalEngineBridge_Delay(ms);
        return;
    }
    shim_ticks += ms;
#endif
}

Uint64 SDL_GetPerformanceCounter(void)
{
    return SDL_GetTicks();
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return 1000;
}

int SDL_PollEvent(SDL_Event *event)
{
    if (shim_event_head == shim_event_tail) {
        SDL_Event target_event;
#if PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS
        if (!PalEngineBridge_PollEvent(&target_event)) {
            return 0;
        }
#else
        if (PalEngineBridge_PollEvent == NULL || !PalEngineBridge_PollEvent(&target_event)) {
            return 0;
        }
#endif
        apply_keyboard_event(&target_event);
        if (event != NULL) {
            *event = target_event;
        }
        return 1;
    }
    if (event != NULL) {
        *event = shim_events[shim_event_tail % SHIM_EVENT_QUEUE];
        apply_keyboard_event(event);
    } else {
        apply_keyboard_event(&shim_events[shim_event_tail % SHIM_EVENT_QUEUE]);
    }
    shim_event_tail++;
    return 1;
}

int SDL_PushEvent(SDL_Event *event)
{
    if (event == NULL || shim_event_head - shim_event_tail >= SHIM_EVENT_QUEUE) {
        return 0;
    }
    shim_events[shim_event_head % SHIM_EVENT_QUEUE] = *event;
    shim_event_head++;
    apply_keyboard_event(event);
    return 1;
}

const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
    if (numkeys != NULL) {
        *numkeys = SHIM_KEYBOARD_KEYS;
    }
    return shim_keyboard;
}

int SDL_GetScancodeFromKey(int key)
{
    /*
     * SDL 1.2 key symbols use 256..383 for keypad/navigation/modifier
     * keys.  The shim does not need a 512-byte identity table: fold that
     * disjoint range into 128..255 and keep ASCII in 0..127.
     */
    if (key >= 0 && key < 128 && key < SHIM_KEYBOARD_KEYS) {
        return key;
    }
    if (key >= 256 && key < 384 && key - 128 < SHIM_KEYBOARD_KEYS) {
        return key - 128;
    }
    return 0;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
    (void)title;
    (void)x;
    (void)y;
    (void)flags;
    shim_window.w = w;
    shim_window.h = h;
    return &shim_window;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    (void)window;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
    (void)window;
    (void)index;
    (void)flags;
    return &shim_renderer;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    (void)renderer;
}

const char *SDL_GetRendererName(SDL_Renderer *renderer)
{
    (void)renderer;
    return "cores3se-native-shim";
}

int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    (void)renderer;
    if (w != NULL) {
        *w = shim_window.w > 0 ? shim_window.w : 320;
    }
    if (h != NULL) {
        *h = shim_window.h > 0 ? shim_window.h : 240;
    }
    return 0;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
#if defined(PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY)
    (void)renderer;
    (void)format;
    (void)access;
    (void)w;
    (void)h;
    return NULL;
#else
    int i;
    (void)renderer;
    (void)format;
    (void)access;
    for (i = 0; i < SHIM_MAX_TEXTURES; i++) {
        SDL_Texture *texture = &shim_textures[i];
        if (texture->used) {
            continue;
        }
        memset(texture, 0, sizeof(*texture));
        texture->used = 1;
        texture->w = w;
        texture->h = h;
        texture->pitch = w * 4;
        return texture;
    }
    return NULL;
#endif
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (texture != NULL) {
        if (shim_present_texture == texture) {
            shim_present_texture = NULL;
        }
        texture->used = 0;
    }
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
    (void)rect;
#if defined(PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY)
    (void)texture;
    (void)pixels;
    (void)pitch;
    return -1;
#else
    if (texture == NULL || pixels == NULL || pitch == NULL) {
        return -1;
    }
    *pixels = texture->pixels;
    *pitch = texture->pitch;
    return 0;
#endif
}

void SDL_UnlockTexture(SDL_Texture *texture)
{
    (void)texture;
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    (void)renderer;
    return 0;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst)
{
    (void)renderer;
    (void)src;
    (void)dst;
    shim_present_texture = texture;
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    (void)renderer;
#if !defined(PAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY)
#if PAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS
    if (shim_present_texture != NULL) {
#else
    if (PalEngineBridge_RenderPresent != NULL && shim_present_texture != NULL) {
#endif
        PalEngineBridge_RenderPresent(shim_present_texture->pixels,
                                      shim_present_texture->pitch,
                                      shim_present_texture->w,
                                      shim_present_texture->h);
    }
#endif
}

int SDL_SetHint(const char *name, const char *value)
{
    (void)name;
    (void)value;
    return 1;
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    (void)window;
    (void)title;
}

int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags)
{
    (void)window;
    (void)flags;
    return 0;
}

void SDL_ShowCursor(int toggle)
{
    (void)toggle;
}

int SDL_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid)
{
    (void)messageboxdata;
    if (buttonid != NULL) {
        *buttonid = 0;
    }
    return SDL_OK;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
    return create_surface_common(flags, width, height, depth, 0, NULL, rmask, gmask, bmask, amask);
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch,
                                      Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
    return create_surface_common(0, width, height, depth, pitch, pixels, rmask, gmask, bmask, amask);
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    ShimSurfaceSlot *slot = find_surface_slot(surface);
    if (slot != NULL) {
        slot->used = 0;
    }
}

SDL_Palette *SDL_AllocPalette(int ncolors)
{
    int i;
    if (ncolors <= 0 || ncolors > 256) {
        return NULL;
    }
    for (i = 0; i < SHIM_MAX_PALETTES; i++) {
        ShimPaletteSlot *slot = &shim_palettes[i];
        if (slot->used) {
            continue;
        }
        memset(slot, 0, sizeof(*slot));
        slot->used = 1;
        slot->palette.ncolors = ncolors;
        slot->palette.colors = slot->colors;
        return &slot->palette;
    }
    return NULL;
}

void SDL_FreePalette(SDL_Palette *palette)
{
    int i;
    for (i = 0; i < SHIM_MAX_PALETTES; i++) {
        if (shim_palettes[i].used && &shim_palettes[i].palette == palette) {
            shim_palettes[i].used = 0;
            return;
        }
    }
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors)
{
    if (palette == NULL || colors == NULL || firstcolor < 0 || ncolors < 0 ||
        firstcolor + ncolors > palette->ncolors) {
        return -1;
    }
    memcpy(palette->colors + firstcolor, colors, (size_t)ncolors * sizeof(colors[0]));
    return 0;
}

int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
    if (surface == NULL || surface->format == NULL) {
        return -1;
    }
    surface->format->palette = palette;
    return 0;
}

int SDL_SetSurfaceColorMod(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b)
{
    (void)surface;
    (void)r;
    (void)g;
    (void)b;
    return 0;
}

int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key)
{
    (void)surface;
    (void)flag;
    (void)key;
    return 0;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
    (void)format;
    return 0xff000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

void SDL_GetRGB(Uint32 pixel, const SDL_PixelFormat *format, Uint8 *r, Uint8 *g, Uint8 *b)
{
    if (format != NULL && format->BytesPerPixel == 1 && format->palette != NULL &&
        pixel < (Uint32)format->palette->ncolors) {
        SDL_Color color = format->palette->colors[pixel];
        if (r != NULL) {
            *r = color.r;
        }
        if (g != NULL) {
            *g = color.g;
        }
        if (b != NULL) {
            *b = color.b;
        }
        return;
    }
    if (r != NULL) {
        *r = (Uint8)(pixel >> 16);
    }
    if (g != NULL) {
        *g = (Uint8)(pixel >> 8);
    }
    if (b != NULL) {
        *b = (Uint8)pixel;
    }
}

int SDL_LockSurface(SDL_Surface *surface)
{
    (void)surface;
    return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
    (void)surface;
}

/*
 * Copy an unscaled indexed surface without going through the generic pixel
 * accessors.  SDL_BlitSurface is used for screen backup/restore on the
 * embedded targets, so this path must also handle clipped rectangles and the
 * (rare) case where source and destination overlap.
 */
static int blit_indexed_1to1(const SDL_Surface *src, const SDL_Rect *s,
                             SDL_Surface *dst, const SDL_Rect *d)
{
    int64_t x_first;
    int64_t x_after;
    int64_t y_first;
    int64_t y_after;
    int y;
    int y_step = 1;
    int may_overlap = 0;

    if (src == NULL || dst == NULL || s == NULL || d == NULL ||
        src->format == NULL || dst->format == NULL ||
        src->format->BytesPerPixel != 1 ||
        dst->format->BytesPerPixel != 1 ||
        src->pixels == NULL || dst->pixels == NULL ||
        src->pitch < src->w || dst->pitch < dst->w ||
        s->w != d->w || s->h != d->h || s->w <= 0 || s->h <= 0) {
        return 0;
    }

    x_first = 0;
    x_after = d->w;
    if (-((int64_t)d->x) > x_first) {
        x_first = -((int64_t)d->x);
    }
    if (-((int64_t)s->x) > x_first) {
        x_first = -((int64_t)s->x);
    }
    if ((int64_t)dst->w - d->x < x_after) {
        x_after = (int64_t)dst->w - d->x;
    }
    if ((int64_t)src->w - s->x < x_after) {
        x_after = (int64_t)src->w - s->x;
    }

    y_first = 0;
    y_after = d->h;
    if (-((int64_t)d->y) > y_first) {
        y_first = -((int64_t)d->y);
    }
    if (-((int64_t)s->y) > y_first) {
        y_first = -((int64_t)s->y);
    }
    if ((int64_t)dst->h - d->y < y_after) {
        y_after = (int64_t)dst->h - d->y;
    }
    if ((int64_t)src->h - s->y < y_after) {
        y_after = (int64_t)src->h - s->y;
    }

    if (x_first >= x_after || y_first >= y_after) {
        return 1;
    }

    if (src->pitch > 0 && dst->pitch > 0 && src->h > 0 && dst->h > 0) {
        uintptr_t src_begin = (uintptr_t)src->pixels;
        uintptr_t dst_begin = (uintptr_t)dst->pixels;
        size_t src_span;
        size_t dst_span;

#if defined(__GNUC__)
        if (__builtin_mul_overflow((size_t)src->h, (size_t)src->pitch,
                &src_span) ||
            __builtin_mul_overflow((size_t)dst->h, (size_t)dst->pitch,
                &dst_span)) {
            may_overlap = 1;
        } else {
            uintptr_t src_after = src_begin + src_span;
            uintptr_t dst_after = dst_begin + dst_span;

            if (src_after < src_begin || dst_after < dst_begin ||
                (src_begin < dst_after && dst_begin < src_after)) {
                may_overlap = 1;
            }
        }
#else
        may_overlap = 1;
#endif
    } else {
        may_overlap = 1;
    }

    if (may_overlap) {
        const Uint8 *src_first = (const Uint8 *)src->pixels +
            (size_t)(s->y + (int)y_first) * (size_t)src->pitch +
            (size_t)(s->x + (int)x_first);
        const Uint8 *dst_first = (const Uint8 *)dst->pixels +
            (size_t)(d->y + (int)y_first) * (size_t)dst->pitch +
            (size_t)(d->x + (int)x_first);

        /* Copy bottom-up when the destination starts below the source. */
        if ((uintptr_t)dst_first > (uintptr_t)src_first) {
            y_step = -1;
        }
    }

    y = y_step > 0 ? (int)y_first : (int)y_after - 1;
    for (;;) {
        int sx = s->x + (int)x_first;
        int dx = d->x + (int)x_first;
        int sy = s->y + y;
        int dy = d->y + y;
        const Uint8 *source = (const Uint8 *)src->pixels +
            (size_t)sy * (size_t)src->pitch + (size_t)sx;
        Uint8 *destination = (Uint8 *)dst->pixels +
            (size_t)dy * (size_t)dst->pitch + (size_t)dx;
        size_t row_bytes = (size_t)(x_after - x_first);

        if (may_overlap) {
            memmove(destination, source, row_bytes);
        } else {
            memcpy(destination, source, row_bytes);
        }

        if (y_step > 0) {
            y++;
            if ((int64_t)y >= y_after) {
                break;
            }
        } else {
            y--;
            if ((int64_t)y < y_first) {
                break;
            }
        }
    }
    return 1;
}

static void fill_surface_row(Uint8 *destination, size_t pixels,
                             Uint8 bytes_per_pixel, Uint32 color)
{
    size_t x;

    if (bytes_per_pixel == 1) {
        memset(destination, (Uint8)color, pixels);
        return;
    }
    for (x = 0; x < pixels; x++) {
        Uint8 *pixel = destination + x * bytes_per_pixel;

        pixel[0] = (Uint8)color;
        pixel[1] = (Uint8)(color >> 8);
        if (bytes_per_pixel >= 4) {
            pixel[2] = (Uint8)(color >> 16);
            pixel[3] = (Uint8)(color >> 24);
        }
    }
}

static int advance_blit_coordinate(int coordinate, int step,
                                   int remainder, int denominator,
                                   uint32_t *error)
{
    coordinate += step;
    if (remainder != 0) {
        *error += (uint32_t)remainder;
        if (*error >= (uint32_t)denominator) {
            *error -= (uint32_t)denominator;
            coordinate++;
        }
    }
    return coordinate;
}

int SDL_FillRect(SDL_Surface *surface, const SDL_Rect *rect, Uint32 color)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int y;
    if (surface == NULL) {
        return -1;
    }
    x0 = rect != NULL ? rect->x : 0;
    y0 = rect != NULL ? rect->y : 0;
    x1 = rect != NULL ? rect->x + rect->w : surface->w;
    y1 = rect != NULL ? rect->y + rect->h : surface->h;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > surface->w) {
        x1 = surface->w;
    }
    if (y1 > surface->h) {
        y1 = surface->h;
    }

    if (x0 >= x1 || y0 >= y1) {
        return 0;
    }

    if (surface->pixels != NULL && surface->format != NULL &&
        (surface->format->BytesPerPixel == 1 ||
         surface->format->BytesPerPixel == 2 ||
         surface->format->BytesPerPixel == 4) &&
        surface->pitch >= surface->w) {
        for (y = y0; y < y1; y++) {
            Uint8 *destination = (Uint8 *)surface->pixels +
                (size_t)y * (size_t)surface->pitch +
                (size_t)x0 * surface->format->BytesPerPixel;
            fill_surface_row(destination, (size_t)(x1 - x0),
                surface->format->BytesPerPixel, color);
        }
        return 0;
    }

    {
        int x;
        for (y = y0; y < y1; y++) {
            for (x = x0; x < x1; x++) {
                write_pixel(surface, x, y, color);
            }
        }
    }
    return 0;
}

int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    SDL_Rect s;
    SDL_Rect d;
    int y;
    int x_step;
    int x_remainder;
    int y_step;
    int y_remainder;
    uint32_t y_error = 0;
    int y_offset = 0;
    if (src == NULL || dst == NULL) {
        return -1;
    }
    s.x = srcrect != NULL ? srcrect->x : 0;
    s.y = srcrect != NULL ? srcrect->y : 0;
    s.w = srcrect != NULL ? srcrect->w : src->w;
    s.h = srcrect != NULL ? srcrect->h : src->h;
    d.x = dstrect != NULL ? dstrect->x : 0;
    d.y = dstrect != NULL ? dstrect->y : 0;
    d.w = dstrect != NULL && dstrect->w > 0 ? dstrect->w : s.w;
    d.h = dstrect != NULL && dstrect->h > 0 ? dstrect->h : s.h;
    if (s.w <= 0 || s.h <= 0 || d.w <= 0 || d.h <= 0) {
        return 0;
    }

    if (blit_indexed_1to1(src, &s, dst, &d)) {
        return 0;
    }

    /* One division per axis initializes an exact nearest-neighbour walk. */
    x_step = s.w / d.w;
    x_remainder = s.w - x_step * d.w;
    y_step = s.h / d.h;
    y_remainder = s.h - y_step * d.h;

    for (y = 0; y < d.h; y++) {
        int64_t dy = (int64_t)d.y + y;
        int64_t sy = (int64_t)s.y + y_offset;

        if (dy >= 0 && dy < dst->h && sy >= 0 && sy < src->h) {
            int x;
            int x_offset = 0;
            uint32_t x_error = 0;

            for (x = 0; x < d.w; x++) {
                int64_t dx = (int64_t)d.x + x;
                int64_t sx = (int64_t)s.x + x_offset;

                if (dx >= 0 && dx < dst->w &&
                    sx >= 0 && sx < src->w) {
                    Uint32 value = read_pixel(src, (int)sx, (int)sy);
                    write_pixel(dst, (int)dx, (int)dy,
                        convert_pixel(src, dst, value));
                }
                x_offset = advance_blit_coordinate(x_offset, x_step,
                    x_remainder, d.w, &x_error);
            }
        }
        y_offset = advance_blit_coordinate(y_offset, y_step,
            y_remainder, d.h, &y_error);
    }
    return 0;
}

int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    return SDL_UpperBlit(src, srcrect, dst, dstrect);
}

int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    return SDL_UpperBlit(src, srcrect, dst, dstrect);
}

void SDL_UpdateRect(SDL_Surface *surface, int x, int y, int w, int h)
{
    (void)surface;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

int SDL_SaveBMP(SDL_Surface *surface, const char *file)
{
    (void)surface;
    (void)file;
    return -1;
}

SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc)
{
    (void)src;
    (void)freesrc;
    return NULL;
}

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
    (void)mem;
    (void)size;
    return NULL;
}

Uint16 SDL_Swap16(Uint16 x)
{
    return (Uint16)((x << 8) | (x >> 8));
}

Uint32 SDL_Swap32(Uint32 x)
{
    return ((x & 0x000000ffu) << 24) |
           ((x & 0x0000ff00u) << 8) |
           ((x & 0x00ff0000u) >> 8) |
           ((x & 0xff000000u) >> 24);
}

char *SDL_getenv(const char *name)
{
    return getenv(name);
}

int SDL_setenv(const char *name, const char *value, int overwrite)
{
    (void)name;
    (void)value;
    (void)overwrite;
    return 0;
}

int SDL_atoi(const char *text)
{
    return atoi(text);
}

int SDL_strcasecmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) {
            return ca - cb;
        }
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int SDL_strncasecmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb || ca == 0 || cb == 0) {
            return ca - cb;
        }
    }
    return 0;
}

int SDL_strncmp(const char *a, const char *b, size_t n)
{
    return strncmp(a, b, n);
}

char *SDL_strrchr(const char *s, int c)
{
    return (char *)strrchr(s, c);
}

void *SDL_memcpy(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}

void *SDL_memset(void *dst, int c, size_t n)
{
    return memset(dst, c, n);
}

size_t SDL_strlen(const char *s)
{
    return strlen(s);
}
