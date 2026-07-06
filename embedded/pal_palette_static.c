#include "pal_palette_static.h"

#include "pal_memory.h"

#include <stddef.h>
#include <string.h>

static void reset_buffer(PalPaletteBuffer *buffer)
{
    if (buffer != NULL) {
        buffer->data = NULL;
        buffer->size = 0;
    }
}

bool PalPalette_LoadCurrentRgb(const uint8_t *rgb, uint32_t size, PalPaletteBuffer *buffer)
{
    if (buffer == NULL || rgb == NULL || size != PAL_PALETTE_RGB_BYTES) {
        reset_buffer(buffer);
        return false;
    }

    memcpy(pal_sram_palette_current, rgb, PAL_SRAM_PALETTE_RGB_BYTES);
    buffer->data = pal_sram_palette_current;
    buffer->size = PAL_SRAM_PALETTE_RGB_BYTES;
    return true;
}

bool PalPalette_LoadCurrentRgb6(const uint8_t *rgb6, uint32_t size, PalPaletteBuffer *buffer)
{
    uint32_t i;

    if (buffer == NULL || rgb6 == NULL || size != PAL_PALETTE_RGB_BYTES) {
        reset_buffer(buffer);
        return false;
    }

    for (i = 0; i < PAL_SRAM_PALETTE_RGB_BYTES; i++) {
        pal_sram_palette_current[i] = (uint8_t)(rgb6[i] << 2);
    }
    buffer->data = pal_sram_palette_current;
    buffer->size = PAL_SRAM_PALETTE_RGB_BYTES;
    return true;
}

bool PalPalette_BlendRgb(const uint8_t *target_rgb, uint32_t target_size, uint8_t step, uint8_t total, PalPaletteBuffer *buffer)
{
    uint32_t i;

    if (buffer == NULL || target_rgb == NULL || target_size != PAL_PALETTE_RGB_BYTES || total == 0 || step > total) {
        reset_buffer(buffer);
        return false;
    }

    for (i = 0; i < PAL_SRAM_PALETTE_RGB_BYTES; i++) {
        uint16_t source = pal_sram_palette_current[i];
        uint16_t target = target_rgb[i];
        pal_sram_palette_work[i] = (uint8_t)((source * (uint16_t)(total - step) + target * step) / total);
    }

    buffer->data = pal_sram_palette_work;
    buffer->size = PAL_SRAM_PALETTE_RGB_BYTES;
    return true;
}

bool PalPalette_ScaleCurrent(uint8_t step, uint8_t total, PalPaletteBuffer *buffer)
{
    uint32_t i;

    if (buffer == NULL || total == 0 || step > total) {
        reset_buffer(buffer);
        return false;
    }

    for (i = 0; i < PAL_SRAM_PALETTE_RGB_BYTES; i++) {
        pal_sram_palette_work[i] = (uint8_t)((uint16_t)pal_sram_palette_current[i] * step / total);
    }

    buffer->data = pal_sram_palette_work;
    buffer->size = PAL_SRAM_PALETTE_RGB_BYTES;
    return true;
}

bool PalPalette_FillColor(const uint8_t *rgb, uint32_t size, uint8_t color_index, PalPaletteBuffer *buffer)
{
    uint32_t i;
    const uint8_t *color;

    if (buffer == NULL || rgb == NULL || size != PAL_PALETTE_RGB_BYTES) {
        reset_buffer(buffer);
        return false;
    }

    color = rgb + (uint32_t)color_index * 3u;
    for (i = 0; i < PAL_PALETTE_COLORS; i++) {
        pal_sram_palette_work[i * 3u] = color[0];
        pal_sram_palette_work[i * 3u + 1u] = color[1];
        pal_sram_palette_work[i * 3u + 2u] = color[2];
    }

    buffer->data = pal_sram_palette_work;
    buffer->size = PAL_SRAM_PALETTE_RGB_BYTES;
    return true;
}
