#ifndef PAL_CORES3SE_NATIVE_SDL_H
#define PAL_CORES3SE_NATIVE_SDL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL 0
#define SDL_VERSION_ATLEAST(X, Y, Z) \
    ((SDL_MAJOR_VERSION > (X)) || \
     (SDL_MAJOR_VERSION == (X) && SDL_MINOR_VERSION > (Y)) || \
     (SDL_MAJOR_VERSION == (X) && SDL_MINOR_VERSION == (Y) && SDL_PATCHLEVEL >= (Z)))

#define SDL_DECLSPEC
#define SDLCALL
#define SDL_INLINE inline
#if defined(__GNUC__)
#define SDL_FORCE_INLINE static inline __attribute__((always_inline))
#else
#define SDL_FORCE_INLINE static inline
#endif

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;
typedef int SDL_bool;

#define SDL_TRUE 1
#define SDL_FALSE 0
#define SDL_ENABLE 1
#define SDL_DISABLE 0
#ifndef SDL_OK
#define SDL_OK 0
#endif
#ifndef SDL_FAIL
#define SDL_FAIL (-1)
#endif

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_CDROM 0u
#define SDL_INIT_JOYSTICK 0x00000200u
#define SDL_INIT_NOPARACHUTE 0u

#define SDL_WINDOW_SHOWN 0x00000004u
#define SDL_WINDOW_RESIZABLE 0x00000020u
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x00001001u
#define SDL_WINDOWPOS_UNDEFINED 0

#define SDL_SWSURFACE 0u
#define SDL_HWSURFACE 0u
#define SDL_PREALLOC 0x01000000u
#define SDL_RLEACCEL 0u

#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_RENDERER_ACCELERATED 0x00000002u
#define SDL_RENDERER_PRESENTVSYNC 0x00000004u

#define SDL_QUIT 0x100u
#define SDL_KEYDOWN 0x300u
#define SDL_KEYUP 0x301u
#define SDL_MOUSEBUTTONDOWN 0x401u
#define SDL_MOUSEBUTTONUP 0x402u
#define SDL_JOYAXISMOTION 0x600u
#define SDL_JOYHATMOTION 0x602u
#define SDL_JOYBUTTONDOWN 0x603u
#define SDL_JOYDEVICEADDED 0x605u
#define SDL_JOYDEVICEREMOVED 0x606u
#define SDL_FINGERDOWN 0x700u
#define SDL_FINGERUP 0x701u
#define SDL_FINGERMOTION 0x702u
#define SDL_WINDOWEVENT 0x200u
#define SDL_WINDOWEVENT_SIZE_CHANGED 0x05u
#define SDL_APP_WILLENTERBACKGROUND 0x1000u
#define SDL_APP_DIDENTERFOREGROUND 0x1002u
#define SDL_USEREVENT 0x8000u

#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP 0x01
#define SDL_HAT_RIGHT 0x02
#define SDL_HAT_DOWN 0x04
#define SDL_HAT_LEFT 0x08
#define SDL_HAT_RIGHTUP (SDL_HAT_RIGHT | SDL_HAT_UP)
#define SDL_HAT_RIGHTDOWN (SDL_HAT_RIGHT | SDL_HAT_DOWN)
#define SDL_HAT_LEFTUP (SDL_HAT_LEFT | SDL_HAT_UP)
#define SDL_HAT_LEFTDOWN (SDL_HAT_LEFT | SDL_HAT_DOWN)

#define SDLK_UP 273
#define SDLK_DOWN 274
#define SDLK_RIGHT 275
#define SDLK_LEFT 276
#define SDLK_INSERT 277
#define SDLK_HOME 278
#define SDLK_END 279
#define SDLK_PAGEUP 280
#define SDLK_PAGEDOWN 281
#define SDLK_RETURN 13
#define SDLK_ESCAPE 27
#define SDLK_SPACE 32
#define SDLK_LALT 308
#define SDLK_RALT 307
#define SDLK_LCTRL 306
#define SDLK_a 'a'
#define SDLK_d 'd'
#define SDLK_e 'e'
#define SDLK_f 'f'
#define SDLK_p 'p'
#define SDLK_q 'q'
#define SDLK_r 'r'
#define SDLK_s 's'
#define SDLK_w 'w'
#define SDLK_F1 0x4000003au
#define SDLK_F4 0x4000003du
#define KMOD_ALT 0x0300u
#define SDLK_KP_0 256
#define SDLK_KP_1 257
#define SDLK_KP_2 258
#define SDLK_KP_3 259
#define SDLK_KP_4 260
#define SDLK_KP_5 261
#define SDLK_KP_6 262
#define SDLK_KP_7 263
#define SDLK_KP_8 264
#define SDLK_KP_9 265
#define SDLK_KP_ENTER 271

#define AUDIO_U8 0x0008
#define AUDIO_S8 0x8008
#define AUDIO_U16LSB 0x0010
#define AUDIO_S16LSB 0x8010
#define AUDIO_U16MSB 0x1010
#define AUDIO_S16MSB 0x9010
#define AUDIO_S16SYS AUDIO_S16LSB
#define AUDIO_S32LSB 0x8020
#define AUDIO_S32MSB 0x9020
#define AUDIO_F32SYS 0x8120
#define SDL_MIX_MAXVOLUME 128
#define SDL_AUDIO_BITSIZE(x) ((x) & 0xff)

#define SDL_TICKS_PASSED(A, B) ((Sint32)((B) - (A)) <= 0)
#define SDL_MUSTLOCK(surface) (0)

typedef struct SDL_Color {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
} SDL_Color;

typedef struct SDL_Palette {
    int ncolors;
    SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    Uint32 format;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint32 Rmask;
    Uint32 Gmask;
    Uint32 Bmask;
    Uint32 Amask;
    SDL_Palette *palette;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w;
    int h;
    int pitch;
    void *pixels;
} SDL_Surface;

typedef struct SDL_Rect {
    int x;
    int y;
    int w;
    int h;
} SDL_Rect;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Joystick SDL_Joystick;
typedef int SDL_TouchID;
typedef int SDL_FingerID;
typedef int SDL_AudioDeviceID;
typedef uint16_t SDL_AudioFormat;
typedef struct SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint16 samples;
    void (SDLCALL *callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;

#define SDL_MESSAGEBOX_ERROR 0x00000010u
#define SDL_MESSAGEBOX_WARNING 0x00000020u
typedef struct SDL_MessageBoxButtonData {
    Uint32 flags;
    int buttonid;
    const char *text;
} SDL_MessageBoxButtonData;

typedef struct SDL_MessageBoxData {
    Uint32 flags;
    SDL_Window *window;
    const char *title;
    const char *message;
    int numbuttons;
    const SDL_MessageBoxButtonData *buttons;
    const void *colorScheme;
} SDL_MessageBoxData;

typedef struct SDL_Keysym {
    int sym;
    Uint16 mod;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent {
    Uint32 type;
    Uint8 repeat;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type;
    int x;
    int y;
    Uint8 button;
} SDL_MouseButtonEvent;

typedef struct SDL_JoyAxisEvent {
    Uint32 type;
    Uint8 axis;
    Sint16 value;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyHatEvent {
    Uint32 type;
    Uint8 hat;
    Uint8 value;
} SDL_JoyHatEvent;

typedef struct SDL_JoyButtonEvent {
    Uint32 type;
    Uint8 button;
} SDL_JoyButtonEvent;

typedef struct SDL_TouchFingerEvent {
    Uint32 type;
    SDL_TouchID touchId;
    SDL_FingerID fingerId;
    float x;
    float y;
} SDL_TouchFingerEvent;

typedef struct SDL_UserEvent {
    Uint32 type;
    Sint32 code;
    void *data1;
    void *data2;
} SDL_UserEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint8 event;
    int data1;
    int data2;
} SDL_WindowEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_MouseButtonEvent button;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyHatEvent jhat;
    SDL_JoyButtonEvent jbutton;
    SDL_TouchFingerEvent tfinger;
    SDL_UserEvent user;
    SDL_WindowEvent window;
} SDL_Event;

typedef struct SDL_RWops SDL_RWops;

int SDL_Init(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);
void SDL_SetError(const char *fmt, ...);
Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
int SDL_PollEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
const Uint8 *SDL_GetKeyboardState(int *numkeys);
int SDL_GetScancodeFromKey(int key);

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *window);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
const char *SDL_GetRendererName(SDL_Renderer *renderer);
int SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture *texture);
int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch);
void SDL_UnlockTexture(SDL_Texture *texture);
int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst);
void SDL_RenderPresent(SDL_Renderer *renderer);
int SDL_SetHint(const char *name, const char *value);
void SDL_SetWindowTitle(SDL_Window *window, const char *title);
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags);
void SDL_ShowCursor(int toggle);
int SDL_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid);

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask);
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch,
                                      Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask);
void SDL_FreeSurface(SDL_Surface *surface);
SDL_Palette *SDL_AllocPalette(int ncolors);
void SDL_FreePalette(SDL_Palette *palette);
int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors, int firstcolor, int ncolors);
int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
int SDL_SetSurfaceColorMod(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
void SDL_GetRGB(Uint32 pixel, const SDL_PixelFormat *format, Uint8 *r, Uint8 *g, Uint8 *b);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
int SDL_FillRect(SDL_Surface *surface, const SDL_Rect *rect, Uint32 color);
int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
void SDL_UpdateRect(SDL_Surface *surface, int x, int y, int w, int h);
int SDL_SaveBMP(SDL_Surface *surface, const char *file);
SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc);
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);

Uint16 SDL_Swap16(Uint16 x);
Uint32 SDL_Swap32(Uint32 x);
#define SDL_SwapLE16(x) ((Uint16)(x))
#define SDL_SwapLE32(x) ((Uint32)(x))
#define SDL_SwapBE16(x) SDL_Swap16((Uint16)(x))
#define SDL_SwapBE32(x) SDL_Swap32((Uint32)(x))

char *SDL_getenv(const char *name);
int SDL_setenv(const char *name, const char *value, int overwrite);
int SDL_atoi(const char *text);
int SDL_strcasecmp(const char *a, const char *b);
int SDL_strncasecmp(const char *a, const char *b, size_t n);
int SDL_strncmp(const char *a, const char *b, size_t n);
char *SDL_strrchr(const char *s, int c);
void *SDL_memcpy(void *dst, const void *src, size_t n);
void *SDL_memset(void *dst, int c, size_t n);
size_t SDL_strlen(const char *s);

#define SDL_zero(x) SDL_memset(&(x), 0, sizeof(x))
#define SDL_zerop(x) SDL_memset((x), 0, sizeof(*(x)))
#define SDL_zeroa(x) SDL_memset((x), 0, sizeof(x))

#ifdef __cplusplus
}
#endif

#endif
