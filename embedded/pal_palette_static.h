#ifndef PAL_PALETTE_STATIC_H
#define PAL_PALETTE_STATIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_PALETTE_COLORS 256u
#define PAL_PALETTE_RGB_BYTES (PAL_PALETTE_COLORS * 3u)

typedef struct PalPaletteBuffer {
    uint8_t *data;
    uint32_t size;
} PalPaletteBuffer;

bool PalPalette_LoadCurrentRgb(const uint8_t *rgb, uint32_t size, PalPaletteBuffer *buffer);
bool PalPalette_BlendRgb(const uint8_t *target_rgb, uint32_t target_size, uint8_t step, uint8_t total, PalPaletteBuffer *buffer);
bool PalPalette_ScaleCurrent(uint8_t step, uint8_t total, PalPaletteBuffer *buffer);
bool PalPalette_FillColor(const uint8_t *rgb, uint32_t size, uint8_t color_index, PalPaletteBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif
