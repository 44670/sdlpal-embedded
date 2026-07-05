#include "pal_video_static.h"

#include "pal_memory.h"

#include <stddef.h>
#include <string.h>

static uint16_t pal_video_rgb565[256];
typedef char pal_video_dma_fits[(PAL_SRAM_DISPLAY_DMA_BYTES >= PAL_VIDEO_WIDTH * 2u) ? 1 : -1];
typedef char pal_video_big_buffer_fits[(PAL_SRAM_BIG_BUFFER_BYTES >= PAL_VIDEO_FRAMEBUFFER_BYTES) ? 1 : -1];

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((uint16_t)(r >> 3) << 11) |
           (uint16_t)((uint16_t)(g >> 2) << 5) |
           (uint16_t)(b >> 3);
}

static bool valid_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    return width != 0 && height != 0 &&
           x < PAL_VIDEO_WIDTH && y < PAL_VIDEO_HEIGHT &&
           width <= PAL_VIDEO_WIDTH - x &&
           height <= PAL_VIDEO_HEIGHT - y;
}

static void copy_rect(uint8_t *dst, const uint8_t *src, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint16_t row;

    for (row = 0; row < height; row++) {
        uint32_t offset = ((uint32_t)y + row) * PAL_VIDEO_WIDTH + x;
        memcpy(dst + offset, src + offset, width);
    }
}

void PalVideo_Clear(uint8_t color)
{
    memset(pal_sram_framebuffer, color, PAL_VIDEO_FRAMEBUFFER_BYTES);
}

void PalVideo_SaveScreen(void)
{
    memcpy(pal_psram_screen_bak, pal_sram_framebuffer, PAL_VIDEO_FRAMEBUFFER_BYTES);
}

void PalVideo_RestoreScreen(void)
{
    memcpy(pal_sram_framebuffer, pal_psram_screen_bak, PAL_VIDEO_FRAMEBUFFER_BYTES);
}

void PalVideo_SaveBigBuffer(void)
{
    memcpy(pal_sram_big_buffer, pal_sram_framebuffer, PAL_VIDEO_FRAMEBUFFER_BYTES);
}

void PalVideo_RestoreBigBuffer(void)
{
    memcpy(pal_sram_framebuffer, pal_sram_big_buffer, PAL_VIDEO_FRAMEBUFFER_BYTES);
}

bool PalVideo_SaveRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    if (!valid_rect(x, y, width, height)) {
        return false;
    }
    copy_rect(pal_psram_screen_bak, pal_sram_framebuffer, x, y, width, height);
    return true;
}

bool PalVideo_RestoreRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    if (!valid_rect(x, y, width, height)) {
        return false;
    }
    copy_rect(pal_sram_framebuffer, pal_psram_screen_bak, x, y, width, height);
    return true;
}

bool PalVideo_SetPaletteRgb(uint16_t first_color, uint16_t color_count, const uint8_t *rgb)
{
    uint16_t i;

    if (rgb == NULL || first_color > 256u || color_count > 256u - first_color) {
        return false;
    }

    for (i = 0; i < color_count; i++) {
        pal_video_rgb565[first_color + i] = rgb565(rgb[(uint32_t)i * 3u], rgb[(uint32_t)i * 3u + 1u], rgb[(uint32_t)i * 3u + 2u]);
    }
    return true;
}

bool PalVideo_ConvertLineRgb565(uint16_t y, const uint16_t **line, uint16_t *pixels)
{
    uint32_t x;
    uint16_t *dst = (uint16_t *)pal_sram_display_dma;
    const uint8_t *src;

    if (line == NULL || pixels == NULL || y >= PAL_VIDEO_HEIGHT) {
        return false;
    }

    src = pal_sram_framebuffer + (uint32_t)y * PAL_VIDEO_WIDTH;
    for (x = 0; x < PAL_VIDEO_WIDTH; x++) {
        dst[x] = pal_video_rgb565[src[x]];
    }

    *line = dst;
    *pixels = PAL_VIDEO_WIDTH;
    return true;
}

uint16_t PalVideo_GetRgb565(uint8_t color)
{
    return pal_video_rgb565[color];
}
