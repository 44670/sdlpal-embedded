#include "../main/cores3se_board.h"
#include "../../embedded/pal_memory.h"
#include "../../embedded/pal_video_static.h"

#include "esp_err.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "ff.h"
#include "freertos/task.h"

#include <png.h>

#if PAL_CORES3SE_NATIVE_ENGINE_HOST
#define SDL_MAIN_HANDLED 1
#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#define PAL_NATIVE_HAVE_REAL_SDL 1
#include <SDL2/SDL.h>
#endif
#endif
#ifndef PAL_NATIVE_HAVE_REAL_SDL
#define PAL_NATIVE_HAVE_REAL_SDL 0
#endif
#if PAL_NATIVE_HAVE_REAL_SDL
#include <dlfcn.h>
#endif
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(PAL_HAS_WS_SERVER)
void PAL_WsReviewEnable(void);
int PAL_WsReviewEnabled(void);
void PAL_WsReviewNext(void);
#endif

void app_main(void);

static esp_partition_t pal_native_partition;
static int64_t pal_native_time_us;
static unsigned pal_native_delay_count;
static bool pal_native_wrote_screenshot;
static unsigned pal_native_argb_present_count;
static png_byte pal_native_png_image[CORES3SE_LCD_WIDTH * CORES3SE_LCD_HEIGHT * 3u];

#if defined(PAL_TARGET_XIAOMIAO)
static uint16_t pal_native_logical_width = 160u;
static uint16_t pal_native_logical_height = 128u;
#elif defined(PAL_TARGET_CARDPUTER_ADV)
static uint16_t pal_native_logical_width = 240u;
static uint16_t pal_native_logical_height = 135u;
#else
static uint16_t pal_native_logical_width = CORES3SE_LCD_WIDTH;
static uint16_t pal_native_logical_height = CORES3SE_LCD_HEIGHT;
#endif
static unsigned pal_native_display_scale = 2u;
static bool pal_native_display_requested;

uint16_t PalNativeHost_LogicalWidth(void)
{
    return pal_native_logical_width;
}

uint16_t PalNativeHost_LogicalHeight(void)
{
    return pal_native_logical_height;
}

#if PAL_CORES3SE_NATIVE_ENGINE_HOST && PAL_NATIVE_HAVE_REAL_SDL
typedef struct PalNativeDisplay {
    void *lib;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    bool tried;
    bool ready;
    bool quit;
    bool mouse_down;
    int mouse_x;
    int mouse_y;
    int key_dir;
    bool key_enter;
    bool key_escape;
    uint16_t logical_width;
    uint16_t logical_height;
    int (*Init)(Uint32 flags);
    void (*Quit)(void);
    SDL_Window *(*CreateWindow)(const char *title, int x, int y, int w, int h, Uint32 flags);
    void (*DestroyWindow)(SDL_Window *window);
    SDL_Renderer *(*CreateRenderer)(SDL_Window *window, int index, Uint32 flags);
    void (*DestroyRenderer)(SDL_Renderer *renderer);
    SDL_Texture *(*CreateTexture)(SDL_Renderer *renderer, Uint32 format, int access, int w, int h);
    void (*DestroyTexture)(SDL_Texture *texture);
    int (*UpdateTexture)(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);
    int (*RenderClear)(SDL_Renderer *renderer);
    int (*RenderCopy)(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst);
    void (*RenderPresent)(SDL_Renderer *renderer);
    int (*PollEvent)(SDL_Event *event);
    void (*Delay)(Uint32 ms);
    Uint32 (*GetTicks)(void);
    int (*SetRenderDrawColor)(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
    int (*RenderSetLogicalSize)(SDL_Renderer *renderer, int w, int h);
    void (*GetWindowSize)(SDL_Window *window, int *w, int *h);
    void (*SetWindowTitle)(SDL_Window *window, const char *title);
} PalNativeDisplay;

static PalNativeDisplay pal_native_display;
static uint32_t pal_native_display_lcd_argb[CORES3SE_LCD_WIDTH * CORES3SE_LCD_HEIGHT];
#endif

#if PAL_CORES3SE_NATIVE_ENGINE_HOST
FILE *__real_fopen(const char *path, const char *mode);
int __real_fclose(FILE *stream);
size_t __real_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t __real_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int __real_fseek(FILE *stream, long offset, int whence);
long __real_ftell(FILE *stream);
int __real_fflush(FILE *stream);
int __real_ferror(FILE *stream);
#define native_fopen __real_fopen
#define native_fclose __real_fclose
#define native_fread __real_fread
#define native_fwrite __real_fwrite
#define native_fseek __real_fseek
#define native_ftell __real_ftell
#define native_fflush __real_fflush
#define native_ferror __real_ferror
#else
#define native_fopen fopen
#define native_fclose fclose
#define native_fread fread
#define native_fwrite fwrite
#define native_fseek fseek
#define native_ftell ftell
#define native_fflush fflush
#define native_ferror ferror
#endif

static const char *env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return (value != NULL && value[0] != '\0') ? value : fallback;
}

#if PAL_CORES3SE_NATIVE_ENGINE_HOST && PAL_NATIVE_HAVE_REAL_SDL
static bool env_enabled(const char *name)
{
    const char *value = getenv(name);

    return value != NULL && value[0] != '\0' &&
           strcmp(value, "0") != 0 &&
           strcmp(value, "false") != 0 &&
           strcmp(value, "no") != 0;
}

static void *native_sdl_symbol(const char *name)
{
    void *symbol;

    if (pal_native_display.lib == NULL || name == NULL) {
        return NULL;
    }
    symbol = dlsym(pal_native_display.lib, name);
    if (symbol == NULL) {
        fprintf(stderr, "missing SDL2 symbol: %s\n", name);
    }
    return symbol;
}

#define PAL_NATIVE_LOAD_SDL(name) \
    do { \
        pal_native_display.name = native_sdl_symbol("SDL_" #name); \
        if (pal_native_display.name == NULL) { \
            return false; \
        } \
    } while (0)

static bool native_display_load_sdl(void)
{
    pal_native_display.lib = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (pal_native_display.lib == NULL) {
        pal_native_display.lib = dlopen("libSDL2.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (pal_native_display.lib == NULL) {
        fprintf(stderr, "failed to load SDL2 for native display: %s\n", dlerror());
        return false;
    }

    PAL_NATIVE_LOAD_SDL(Init);
    PAL_NATIVE_LOAD_SDL(Quit);
    PAL_NATIVE_LOAD_SDL(CreateWindow);
    PAL_NATIVE_LOAD_SDL(DestroyWindow);
    PAL_NATIVE_LOAD_SDL(CreateRenderer);
    PAL_NATIVE_LOAD_SDL(DestroyRenderer);
    PAL_NATIVE_LOAD_SDL(CreateTexture);
    PAL_NATIVE_LOAD_SDL(DestroyTexture);
    PAL_NATIVE_LOAD_SDL(UpdateTexture);
    PAL_NATIVE_LOAD_SDL(RenderClear);
    PAL_NATIVE_LOAD_SDL(RenderCopy);
    PAL_NATIVE_LOAD_SDL(RenderPresent);
    PAL_NATIVE_LOAD_SDL(PollEvent);
    PAL_NATIVE_LOAD_SDL(Delay);
    PAL_NATIVE_LOAD_SDL(GetTicks);
    PAL_NATIVE_LOAD_SDL(SetRenderDrawColor);
    PAL_NATIVE_LOAD_SDL(RenderSetLogicalSize);
    PAL_NATIVE_LOAD_SDL(GetWindowSize);
    PAL_NATIVE_LOAD_SDL(SetWindowTitle);
    return true;
}

#undef PAL_NATIVE_LOAD_SDL

static bool native_display_init(void)
{
    const char *title;
    unsigned long width;
    unsigned long height;
    unsigned long scale;

    if (pal_native_display.tried) {
        return pal_native_display.ready;
    }
    pal_native_display.tried = true;

    if (!env_enabled("PAL_CORES3SE_NATIVE_DISPLAY")) {
        return false;
    }
    if (!native_display_load_sdl()) {
        return false;
    }
    width = pal_native_logical_width;
    height = pal_native_logical_height;
    scale = pal_native_display_scale;
    if (width == 0 || width > CORES3SE_LCD_WIDTH ||
        height == 0 || height > CORES3SE_LCD_HEIGHT ||
        scale == 0 || scale > 8) {
        fprintf(stderr, "invalid native SDL display geometry: %lux%lu scale=%lu\n",
                width, height, scale);
        return false;
    }
    pal_native_display.logical_width = (uint16_t)width;
    pal_native_display.logical_height = (uint16_t)height;
    title = env_or_default(
        "PAL_CORES3SE_NATIVE_DISPLAY_TITLE", "SDLPAL native preview");
    if (pal_native_display.Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL2 init failed for native display\n");
        return false;
    }
    pal_native_display.window = pal_native_display.CreateWindow(
        title,
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        (int)(width * scale),
        (int)(height * scale),
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (pal_native_display.window == NULL) {
        fprintf(stderr, "SDL2 window creation failed for native display\n");
        return false;
    }
    /* The host SDL shim exports a no-op SDL_SetWindowTitle symbol.  Calling
     * the explicitly loaded real-SDL function avoids ELF interposition and
     * gives interactive preview windows a usable title. */
    pal_native_display.SetWindowTitle(pal_native_display.window, title);
    pal_native_display.renderer = pal_native_display.CreateRenderer(
        pal_native_display.window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (pal_native_display.renderer == NULL) {
        pal_native_display.renderer = pal_native_display.CreateRenderer(pal_native_display.window, -1, 0);
    }
    if (pal_native_display.renderer == NULL) {
        fprintf(stderr, "SDL2 renderer creation failed for native display\n");
        return false;
    }
    (void)pal_native_display.SetRenderDrawColor(pal_native_display.renderer, 0, 0, 0, 255);
    (void)pal_native_display.RenderSetLogicalSize(
        pal_native_display.renderer,
        pal_native_display.logical_width,
        pal_native_display.logical_height);
    pal_native_display.texture = pal_native_display.CreateTexture(
        pal_native_display.renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        pal_native_display.logical_width,
        pal_native_display.logical_height);
    if (pal_native_display.texture == NULL) {
        fprintf(stderr, "SDL2 texture creation failed for native display\n");
        return false;
    }
    pal_native_display.ready = true;
    return true;
}

static bool native_display_active(void)
{
    return native_display_init();
}

static int64_t native_monotonic_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
}

static void native_display_set_key(int sym, bool down)
{
    int dir = pal_native_display.key_dir;

    switch (sym) {
    case SDLK_UP:
        if (down) {
            dir = 1;
        } else if (dir == 1) {
            dir = 0;
        }
        break;
    case SDLK_DOWN:
        if (down) {
            dir = 2;
        } else if (dir == 2) {
            dir = 0;
        }
        break;
    case SDLK_LEFT:
        if (down) {
            dir = 3;
        } else if (dir == 3) {
            dir = 0;
        }
        break;
    case SDLK_RIGHT:
        if (down) {
            dir = 4;
        } else if (dir == 4) {
            dir = 0;
        }
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        pal_native_display.key_enter = down;
        break;
    case SDLK_ESCAPE:
        pal_native_display.key_escape = down;
        break;
    default:
        break;
    }

    pal_native_display.key_dir = dir;
}

static void native_display_scale_mouse(int *x, int *y)
{
    int ww = pal_native_display.logical_width;
    int wh = pal_native_display.logical_height;

    if (x == NULL || y == NULL) {
        return;
    }
    pal_native_display.GetWindowSize(pal_native_display.window, &ww, &wh);
    if (ww > 0) {
        *x = (*x * pal_native_display.logical_width) / ww;
    }
    if (wh > 0) {
        *y = (*y * pal_native_display.logical_height) / wh;
    }
    if (*x < 0) {
        *x = 0;
    } else if (*x >= pal_native_display.logical_width) {
        *x = pal_native_display.logical_width - 1;
    }
    if (*y < 0) {
        *y = 0;
    } else if (*y >= pal_native_display.logical_height) {
        *y = pal_native_display.logical_height - 1;
    }
}

static void native_display_poll(void)
{
    SDL_Event event;

    if (!native_display_active()) {
        return;
    }
    while (pal_native_display.PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            pal_native_display.quit = true;
            break;
        case SDL_KEYDOWN:
            if (event.key.repeat == 0) {
#if defined(PAL_HAS_WS_SERVER)
                if (event.key.keysym.sym == SDLK_F1 && PAL_WsReviewEnabled()) {
                    PAL_WsReviewNext();
                    break;
                }
#endif
                native_display_set_key(event.key.keysym.sym, true);
            }
            break;
        case SDL_KEYUP:
            native_display_set_key(event.key.keysym.sym, false);
            break;
        case SDL_MOUSEBUTTONDOWN:
            pal_native_display.mouse_down = true;
            pal_native_display.mouse_x = event.button.x;
            pal_native_display.mouse_y = event.button.y;
            native_display_scale_mouse(&pal_native_display.mouse_x, &pal_native_display.mouse_y);
            break;
        case SDL_MOUSEBUTTONUP:
            pal_native_display.mouse_down = false;
            break;
        case SDL_MOUSEMOTION:
            if (pal_native_display.mouse_down) {
                pal_native_display.mouse_x = event.motion.x;
                pal_native_display.mouse_y = event.motion.y;
                native_display_scale_mouse(&pal_native_display.mouse_x, &pal_native_display.mouse_y);
            }
            break;
        default:
            break;
        }
    }
    if (pal_native_display.quit) {
        exit(0);
    }
}

static bool native_display_touch_point(uint16_t *x, uint16_t *y)
{
    native_display_poll();
    if (!native_display_active()) {
        return false;
    }

    if (pal_native_display.key_escape) {
        if (x != NULL) {
            *x = CORES3SE_LCD_WIDTH / 2u;
        }
        if (y != NULL) {
            *y = 0;
        }
        return true;
    }
    if (pal_native_display.key_enter) {
        if (x != NULL) {
            *x = CORES3SE_LCD_WIDTH / 2u;
        }
        if (y != NULL) {
            *y = CORES3SE_PAL_Y_OFFSET + 205u;
        }
        return true;
    }
    if (pal_native_display.key_dir != 0) {
        static const uint16_t points[4][2] = {
            { CORES3SE_LCD_WIDTH / 2u, CORES3SE_PAL_Y_OFFSET + 10u },
            { CORES3SE_LCD_WIDTH / 2u, CORES3SE_PAL_Y_OFFSET + 190u },
            { 10u, CORES3SE_PAL_Y_OFFSET + 100u },
            { CORES3SE_LCD_WIDTH - 10u, CORES3SE_PAL_Y_OFFSET + 100u },
        };
        unsigned index = (unsigned)pal_native_display.key_dir - 1u;
        if (x != NULL) {
            *x = points[index][0];
        }
        if (y != NULL) {
            *y = points[index][1];
        }
        return true;
    }
    if (pal_native_display.mouse_down) {
        if (x != NULL) {
            *x = (uint16_t)(
                (uint32_t)pal_native_display.mouse_x *
                CORES3SE_LCD_WIDTH /
                pal_native_display.logical_width);
        }
        if (y != NULL) {
            *y = (uint16_t)(
                (uint32_t)pal_native_display.mouse_y *
                CORES3SE_LCD_HEIGHT /
                pal_native_display.logical_height);
        }
        return true;
    }
    return false;
}

static bool native_display_present_argb8888(const void *pixels, uint16_t width, uint16_t height, uint16_t pitch)
{
    const uint8_t *src = (const uint8_t *)pixels;
    uint32_t copy_height;
    uint32_t copy_y;

    if (!native_display_active()) {
        return false;
    }
    native_display_poll();
    if (src == NULL || width != pal_native_display.logical_width ||
        height == 0 || height > pal_native_display.logical_height ||
        pitch < (uint16_t)(width * 4u)) {
        return false;
    }

    memset(pal_native_display_lcd_argb, 0,
           (size_t)pal_native_display.logical_width *
           pal_native_display.logical_height * sizeof(uint32_t));
    if (height == 200u && pal_native_display.logical_height == 240u) {
        copy_height = 200u;
        copy_y = CORES3SE_PAL_Y_OFFSET;
    } else if (height <= pal_native_display.logical_height) {
        copy_height = height;
        copy_y = 0;
    } else {
        return false;
    }

    for (uint32_t y = 0; y < copy_height; y++) {
        memcpy(
            pal_native_display_lcd_argb +
                (copy_y + y) * pal_native_display.logical_width,
            src + y * pitch,
            (size_t)width * 4u);
    }

    if (pal_native_display.UpdateTexture(
            pal_native_display.texture,
            NULL,
            pal_native_display_lcd_argb,
            pal_native_display.logical_width * 4) != 0) {
        return false;
    }
    (void)pal_native_display.RenderClear(pal_native_display.renderer);
    (void)pal_native_display.RenderCopy(pal_native_display.renderer, pal_native_display.texture, NULL, NULL);
    pal_native_display.RenderPresent(pal_native_display.renderer);
    return true;
}
#else
static bool native_display_active(void)
{
    return false;
}
#endif

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    default:
        return "ESP_FAIL";
    }
}

int64_t esp_timer_get_time(void)
{
#if PAL_CORES3SE_NATIVE_ENGINE_HOST && PAL_NATIVE_HAVE_REAL_SDL
    static int64_t start_us;

    if (native_display_active()) {
        int64_t now = native_monotonic_us();
        if (start_us == 0) {
            start_us = now;
        }
        return now - start_us;
    }
#endif
    return pal_native_time_us;
}

static bool file_size(const char *path, uint32_t *size)
{
    struct stat st;

    if (path == NULL || size == NULL || stat(path, &st) != 0 || st.st_size <= 0 || st.st_size > UINT32_MAX) {
        return false;
    }
    *size = (uint32_t)st.st_size;
    return true;
}

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    uint32_t size = 0;

    (void)type;
    (void)subtype;
    if (label == NULL || strcmp(label, "pal_nor") != 0) {
        return NULL;
    }
    if (!file_size(env_or_default("PAL_CORES3SE_NATIVE_NOR_PACK", "/tmp/pal_nor_default.pak"), &size)) {
        return NULL;
    }
    pal_native_partition.address = 0x310000u;
    pal_native_partition.size = size;
    pal_native_partition.label = "pal_nor";
    return &pal_native_partition;
}

esp_err_t esp_partition_read(const esp_partition_t *partition, uint32_t src_offset, void *dst, size_t size)
{
    const char *path;
    FILE *fp;
    bool ok;

    if (partition == NULL || dst == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (src_offset > partition->size || size > partition->size - src_offset) {
        return ESP_ERR_INVALID_ARG;
    }

    path = env_or_default("PAL_CORES3SE_NATIVE_NOR_PACK", "/tmp/pal_nor_default.pak");
    fp = native_fopen(path, "rb");
    if (fp == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    ok = native_fseek(fp, (long)src_offset, SEEK_SET) == 0 && native_fread(dst, 1, size, fp) == size;
    native_fclose(fp);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_partition_mmap(
    const esp_partition_t *partition,
    uint32_t offset,
    uint32_t size,
    int memory,
    const void **out_ptr,
    esp_partition_mmap_handle_t *out_handle)
{
    const char *path;
    int fd;
    void *mapped;

    (void)memory;
    if (partition == NULL || out_ptr == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (offset != 0 || size == 0 || size > partition->size) {
        return ESP_ERR_INVALID_ARG;
    }

    path = env_or_default("PAL_CORES3SE_NATIVE_NOR_PACK", "/tmp/pal_nor_default.pak");
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        return ESP_FAIL;
    }
    *out_ptr = mapped;
    *out_handle = mapped;
    return ESP_OK;
}

static const char *map_fatfs_path(const char *path, char *buffer, size_t buffer_size)
{
    const char *save_dir;

    if (path == NULL) {
        return NULL;
    }
    if (strcmp(path, "0:/pal_tf.pak") == 0) {
        return env_or_default("PAL_CORES3SE_NATIVE_TF_PACK", "/tmp/pal_tf_default.pak");
    }
    if (strcmp(path, "0:/pal_core.pak") == 0) {
        return env_or_default("PAL_CORES3SE_NATIVE_NOR_PACK", "/tmp/pal_core.pak");
    }
    if (strcmp(path, "0:/pal_full.pak") == 0) {
        return env_or_default("PAL_CORES3SE_NATIVE_TF_PACK", "/tmp/pal_full.pak");
    }
    if (strncmp(path, "0:/", 3) == 0) {
        save_dir = env_or_default("PAL_CORES3SE_NATIVE_SAVE_DIR", "/mnt/hgfs/deb13/PALSteam/PAL_DOS");
        if (snprintf(buffer, buffer_size, "%s/%s", save_dir, path + 3) >= (int)buffer_size) {
            return NULL;
        }
        return buffer;
    }
    return path;
}

FRESULT f_open(FIL *file, const char *path, unsigned char mode)
{
    char mapped_path[512];
    const char *native_path;
    uint32_t size = 0;
    const char *stdio_mode = "rb";
    bool must_exist;

    if (file == NULL) {
        return FR_INVALID_PARAMETER;
    }
    memset(file, 0, sizeof(*file));
    native_path = map_fatfs_path(path, mapped_path, sizeof(mapped_path));
    if (native_path == NULL) {
        return FR_INVALID_NAME;
    }

    must_exist = (mode & FA_CREATE_ALWAYS) == 0 && (mode & FA_OPEN_APPEND) == 0;
    if (must_exist && !file_size(native_path, &size)) {
        return FR_NO_FILE;
    }
    if ((mode & FA_OPEN_APPEND) != 0) {
        stdio_mode = (mode & FA_READ) != 0 ? "ab+" : "ab";
    } else if ((mode & FA_CREATE_ALWAYS) != 0) {
        stdio_mode = ((mode & FA_READ) != 0 && (mode & FA_WRITE) != 0) ? "wb+" : "wb";
    } else if ((mode & FA_WRITE) != 0) {
        stdio_mode = (mode & FA_READ) != 0 ? "r+b" : "rb+";
    }
    file->fp = native_fopen(native_path, stdio_mode);
    if (file->fp == NULL) {
        return FR_NO_FILE;
    }
    if (!file_size(native_path, &size)) {
        size = 0;
    }
    file->size = size;
    if ((mode & FA_OPEN_APPEND) != 0) {
        (void)native_fseek(file->fp, 0, SEEK_END);
    }
    return FR_OK;
}

FRESULT f_read(FIL *file, void *dst, UINT size, UINT *read_bytes)
{
    size_t got;

    if (read_bytes != NULL) {
        *read_bytes = 0;
    }
    if (file == NULL || file->fp == NULL || (dst == NULL && size != 0)) {
        return FR_INVALID_PARAMETER;
    }
    got = native_fread(dst, 1, size, file->fp);
    if (read_bytes != NULL) {
        *read_bytes = (UINT)got;
    }
    return native_ferror(file->fp) ? FR_INVALID_PARAMETER : FR_OK;
}

FRESULT f_write(FIL *file, const void *src, UINT size, UINT *written_bytes)
{
    size_t done;
    long pos;

    if (written_bytes != NULL) {
        *written_bytes = 0;
    }
    if (file == NULL || file->fp == NULL || (src == NULL && size != 0)) {
        return FR_INVALID_PARAMETER;
    }
    done = native_fwrite(src, 1, size, file->fp);
    if (written_bytes != NULL) {
        *written_bytes = (UINT)done;
    }
    pos = native_ftell(file->fp);
    if (pos >= 0 && (FSIZE_t)pos > file->size) {
        file->size = (FSIZE_t)pos;
    }
    return done == size ? FR_OK : FR_DENIED;
}

FRESULT f_lseek(FIL *file, FSIZE_t offset)
{
    if (file == NULL || file->fp == NULL || offset > file->size) {
        return FR_INVALID_PARAMETER;
    }
    return native_fseek(file->fp, (long)offset, SEEK_SET) == 0 ? FR_OK : FR_INVALID_PARAMETER;
}

FRESULT f_close(FIL *file)
{
    if (file == NULL || file->fp == NULL) {
        return FR_INVALID_PARAMETER;
    }
    native_fclose(file->fp);
    file->fp = NULL;
    return FR_OK;
}

FRESULT f_sync(FIL *file)
{
    if (file == NULL || file->fp == NULL) {
        return FR_INVALID_PARAMETER;
    }
    return native_fflush(file->fp) == 0 ? FR_OK : FR_DENIED;
}

FSIZE_t f_size(FIL *file)
{
    return file != NULL ? file->size : 0;
}

FSIZE_t f_tell(FIL *file)
{
    long pos;

    if (file == NULL || file->fp == NULL) {
        return 0;
    }
    pos = native_ftell(file->fp);
    return pos < 0 ? 0 : (FSIZE_t)pos;
}

bool CoreS3Se_Begin(void)
{
    return true;
}

bool CoreS3Se_MountTf(void)
{
    uint32_t size = 0;
    return file_size(env_or_default("PAL_CORES3SE_NATIVE_TF_PACK", "/tmp/pal_full.pak"), &size);
}

void CoreS3Se_PrepareTfAccess(void)
{
}

void CoreS3Se_PrepareLcdAccess(void)
{
}

static void rgb565_to_rgb(uint16_t color, png_byte *dst)
{
    uint8_t r5 = (uint8_t)((color >> 11) & 0x1fu);
    uint8_t g6 = (uint8_t)((color >> 5) & 0x3fu);
    uint8_t b5 = (uint8_t)(color & 0x1fu);

    dst[0] = (png_byte)((r5 << 3) | (r5 >> 2));
    dst[1] = (png_byte)((g6 << 2) | (g6 >> 4));
    dst[2] = (png_byte)((b5 << 3) | (b5 >> 2));
}

static bool write_lcd_png(const char *path)
{
    FILE *fp;
    png_structp png;
    png_infop info;
    png_bytep rows[CORES3SE_LCD_HEIGHT];
    uint32_t x;
    uint32_t y;

    if (path == NULL || path[0] == '\0') {
        return false;
    }
    for (y = 0; y < CORES3SE_LCD_HEIGHT; y++) {
        png_byte *dst = pal_native_png_image + y * CORES3SE_LCD_WIDTH * 3u;
        rows[y] = dst;
        for (x = 0; x < CORES3SE_LCD_WIDTH; x++) {
            if (y < CORES3SE_PAL_Y_OFFSET || y >= CORES3SE_PAL_Y_OFFSET + 200u) {
                dst[x * 3u + 0u] = 0;
                dst[x * 3u + 1u] = 0;
                dst[x * 3u + 2u] = 0;
            } else {
                uint8_t index = pal_sram_framebuffer[(y - CORES3SE_PAL_Y_OFFSET) * 320u + x];
                rgb565_to_rgb(PalVideo_GetRgb565(index), dst + x * 3u);
            }
        }
    }

    fp = native_fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) {
        native_fclose(fp);
        return false;
    }
    info = png_create_info_struct(png);
    if (info == NULL || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, info != NULL ? &info : NULL);
        native_fclose(fp);
        return false;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, CORES3SE_LCD_WIDTH, CORES3SE_LCD_HEIGHT, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_write_image(png, rows);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    native_fclose(fp);
    return true;
}

bool CoreS3Se_FlushPalFramebuffer(void)
{
    const char *path = env_or_default("PAL_CORES3SE_NATIVE_SCREENSHOT", "build/native/cores3se_scene.png");

    CoreS3Se_PrepareLcdAccess();
    if (!pal_native_wrote_screenshot) {
        pal_native_wrote_screenshot = write_lcd_png(path);
        if (!pal_native_wrote_screenshot) {
            fprintf(stderr, "failed to write PNG screenshot: %s\n", path);
        }
    }
    CoreS3Se_PrepareTfAccess();
    return pal_native_wrote_screenshot;
}

static bool write_argb8888_png(const char *path, const void *pixels, uint16_t width, uint16_t height, uint16_t pitch)
{
    const uint8_t *src = (const uint8_t *)pixels;
    FILE *fp;
    png_structp png;
    png_infop info;
    png_bytep rows[CORES3SE_LCD_HEIGHT];
    uint32_t x;
    uint32_t y;

    if (path == NULL || path[0] == '\0' || src == NULL || width != CORES3SE_LCD_WIDTH ||
        height != 200u || pitch < (uint16_t)(CORES3SE_LCD_WIDTH * 4u)) {
        return false;
    }
    for (y = 0; y < CORES3SE_LCD_HEIGHT; y++) {
        png_byte *dst = pal_native_png_image + y * CORES3SE_LCD_WIDTH * 3u;
        rows[y] = dst;
        if (y < CORES3SE_PAL_Y_OFFSET || y >= CORES3SE_PAL_Y_OFFSET + 200u) {
            memset(dst, 0, CORES3SE_LCD_WIDTH * 3u);
            continue;
        }
        for (x = 0; x < CORES3SE_LCD_WIDTH; x++) {
            const uint8_t *pixel = src + (uint32_t)(y - CORES3SE_PAL_Y_OFFSET) * pitch + x * 4u;
            dst[x * 3u + 0u] = pixel[2];
            dst[x * 3u + 1u] = pixel[1];
            dst[x * 3u + 2u] = pixel[0];
        }
    }

    fp = native_fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) {
        native_fclose(fp);
        return false;
    }
    info = png_create_info_struct(png);
    if (info == NULL || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, info != NULL ? &info : NULL);
        native_fclose(fp);
        return false;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, CORES3SE_LCD_WIDTH, CORES3SE_LCD_HEIGHT, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_write_image(png, rows);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    native_fclose(fp);
    return true;
}

bool CoreS3Se_FlushArgb8888Texture(const void *pixels, uint16_t width, uint16_t height, uint16_t pitch)
{
    const char *path = env_or_default("PAL_CORES3SE_NATIVE_SCREENSHOT", "build/native/cores3se_engine_host.png");
    unsigned screenshot_frame = (unsigned)strtoul(env_or_default("PAL_CORES3SE_NATIVE_SCREENSHOT_FRAME", "899"), NULL, 0);
    unsigned max_presents = (unsigned)strtoul(env_or_default("PAL_CORES3SE_NATIVE_MAX_PRESENTS", "1200"), NULL, 0);

    CoreS3Se_PrepareLcdAccess();
#if PAL_CORES3SE_NATIVE_ENGINE_HOST && PAL_NATIVE_HAVE_REAL_SDL
    if (native_display_active()) {
        if (!native_display_present_argb8888(pixels, width, height, pitch)) {
            CoreS3Se_PrepareTfAccess();
            return false;
        }
        if (!pal_native_wrote_screenshot && getenv("PAL_CORES3SE_NATIVE_SCREENSHOT") != NULL &&
            pal_native_argb_present_count >= screenshot_frame) {
            pal_native_wrote_screenshot = write_argb8888_png(path, pixels, width, height, pitch);
        }
        pal_native_argb_present_count++;
        CoreS3Se_PrepareTfAccess();
        return true;
    }
#endif
    if (!pal_native_wrote_screenshot && pal_native_argb_present_count >= screenshot_frame) {
        pal_native_wrote_screenshot = write_argb8888_png(path, pixels, width, height, pitch);
        if (!pal_native_wrote_screenshot) {
            fprintf(stderr, "failed to write engine PNG screenshot: %s\n", path);
        }
    }
    pal_native_argb_present_count++;
    CoreS3Se_PrepareTfAccess();

    if (pal_native_wrote_screenshot) {
        exit(0);
    }
    if (max_presents != 0 && pal_native_argb_present_count > max_presents) {
        fprintf(stderr, "engine PNG screenshot was not produced after %u presents\n", pal_native_argb_present_count);
        exit(1);
    }
    return true;
}

bool CoreS3Se_TouchPoint(uint16_t *x, uint16_t *y)
{
    const char *touch = getenv("PAL_CORES3SE_NATIVE_TOUCH");

#if PAL_CORES3SE_NATIVE_ENGINE_HOST && PAL_NATIVE_HAVE_REAL_SDL
    if (native_display_touch_point(x, y)) {
        return true;
    }
#endif

    if (touch != NULL && strcmp(touch, "bottom") == 0) {
        if (x != NULL) {
            *x = 160u;
        }
        if (y != NULL) {
            *y = CORES3SE_PAL_Y_OFFSET + 205u;
        }
        return true;
    }
    if (x != NULL) {
        *x = 0;
    }
    if (y != NULL) {
        *y = 0;
    }
    return false;
}

void CoreS3Se_ShowError(const char *line1, const char *line2)
{
    fprintf(stderr, "CoreS3SE error: %s / %s\n", line1 != NULL ? line1 : "", line2 != NULL ? line2 : "");
}

static bool parse_env_uint16(const char *name, uint16_t *out)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0') {
        return false;
    }
    parsed = strtoul(value, &end, 0);
    if (end == value || *end != '\0' || parsed > UINT16_MAX || out == NULL) {
        return false;
    }
    *out = (uint16_t)parsed;
    return true;
}

static bool parse_env_int(const char *name, int *out)
{
    const char *value = getenv(name);
    char *end = NULL;
    long parsed;

    if (value == NULL || value[0] == '\0') {
        return false;
    }
    parsed = strtol(value, &end, 0);
    if (end == value || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX || out == NULL) {
        return false;
    }
    *out = (int)parsed;
    return true;
}

bool CoreS3Se_NativeInitialView(uint16_t *scene_num, int *viewport_x, int *viewport_y)
{
    bool changed = false;

    if (parse_env_uint16("PAL_CORES3SE_NATIVE_SCENE", scene_num)) {
        changed = true;
    }
    if (parse_env_int("PAL_CORES3SE_NATIVE_VIEW_X", viewport_x)) {
        changed = true;
    }
    if (parse_env_int("PAL_CORES3SE_NATIVE_VIEW_Y", viewport_y)) {
        changed = true;
    }
    return changed;
}

void vTaskDelay(TickType_t ticks)
{
    unsigned max_frames = (unsigned)strtoul(env_or_default("PAL_CORES3SE_NATIVE_FRAMES", "1"), NULL, 0);

#if PAL_CORES3SE_NATIVE_ENGINE_HOST && PAL_NATIVE_HAVE_REAL_SDL
    if (native_display_active()) {
        unsigned ms = ticks == 0 ? 1u : (unsigned)ticks;
        native_display_poll();
        pal_native_display.Delay(ms);
        return;
    }
#endif

    pal_native_time_us += (int64_t)ticks * 1000;
    pal_native_delay_count++;
    if (max_frames == 0 || pal_native_delay_count >= max_frames) {
        exit(pal_native_wrote_screenshot ? 0 : 1);
    }
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return 32768u;
}

static bool parse_unsigned_arg(const char *text, unsigned long *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || text[0] == '\0' || value == NULL) {
        return false;
    }
    parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_ui_size(const char *text, uint16_t *width, uint16_t *height)
{
    char *end = NULL;
    unsigned long parsed_width;
    unsigned long parsed_height;

    if (text == NULL || width == NULL || height == NULL) {
        return false;
    }
    parsed_width = strtoul(text, &end, 10);
    if (end == text || (*end != 'x' && *end != 'X')) {
        return false;
    }
    parsed_height = strtoul(end + 1, &end, 10);
    if (*end != '\0' || parsed_width < 80u || parsed_width > 240u ||
        parsed_height < 64u || parsed_height > 135u) {
        return false;
    }
    *width = (uint16_t)parsed_width;
    *height = (uint16_t)parsed_height;
    return true;
}

static int parse_native_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--display") == 0) {
            pal_native_display_requested = true;
        } else if (strcmp(argv[i], "--ui-test") == 0) {
#if defined(PAL_HAS_WS_SERVER)
            PAL_WsReviewEnable();
            pal_native_display_requested = true;
#else
            fprintf(stderr, "--ui-test requires a host build with WebSocket harness support\n");
            return 2;
#endif
        } else if (strcmp(argv[i], "--ui-size") == 0 && i + 1 < argc) {
            if (!parse_ui_size(argv[++i], &pal_native_logical_width,
                    &pal_native_logical_height)) {
                fprintf(stderr, "invalid --ui-size (expected WIDTHxHEIGHT, max 240x135)\n");
                return 2;
            }
            pal_native_display_requested = true;
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            unsigned long scale;
            if (!parse_unsigned_arg(argv[++i], &scale) || scale == 0u || scale > 8u) {
                fprintf(stderr, "invalid --scale (expected 1..8)\n");
                return 2;
            }
            pal_native_display_scale = (unsigned)scale;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: %s [--display] [--ui-test] [--ui-size WIDTHxHEIGHT] [--scale 1..8]\n",
                argv[0]);
            return 1;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int result = parse_native_args(argc, argv);
    if (result != 0) {
        return result == 1 ? 0 : result;
    }
    if (pal_native_display_requested) {
        (void)setenv("PAL_CORES3SE_NATIVE_DISPLAY", "1", 1);
    }
    app_main();
    return 0;
}
