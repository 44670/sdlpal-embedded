#include "../main/cores3se_board.h"
#include "../../embedded/pal_memory.h"
#include "../../embedded/pal_video_static.h"

#include "esp_err.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "ff.h"
#include "freertos/task.h"

#include <png.h>

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

void app_main(void);

static esp_partition_t pal_native_partition;
static int64_t pal_native_time_us;
static unsigned pal_native_delay_count;
static bool pal_native_wrote_screenshot;
static unsigned pal_native_argb_present_count;
static png_byte pal_native_png_image[CORES3SE_LCD_WIDTH * CORES3SE_LCD_HEIGHT * 3u];

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
    if (strncmp(path, "0:/", 3) == 0) {
        save_dir = env_or_default("PAL_CORES3SE_NATIVE_SAVE_DIR", "/mnt/hgfs/deb13/PAL");
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
    return file_size(env_or_default("PAL_CORES3SE_NATIVE_TF_PACK", "/tmp/pal_tf_default.pak"), &size);
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

int main(void)
{
    app_main();
    return 0;
}
