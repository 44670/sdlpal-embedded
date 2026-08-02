#ifndef PAL_NATIVE_UI_H
#define PAL_NATIVE_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PAL_NATIVE_UI_GENERATED_HEADER
#define PAL_NATIVE_UI_GENERATED_HEADER "generated/pal_native_ui_240x135.h"
#endif
#include PAL_NATIVE_UI_GENERATED_HEADER

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PalNativeUiRect {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} PalNativeUiRect;

typedef struct PalNativeUiDialogLayout {
    PalNativeUiRect portrait;
    PalNativeUiRect text;
    int16_t title_x;
    int16_t title_y;
    uint8_t line_height;
    uint8_t page_lines;
} PalNativeUiDialogLayout;

bool PalNativeUi_GetDialogLayout(
    bool lower,
    bool has_portrait,
    PalNativeUiDialogLayout *out);

bool PalNativeUi_GetCenterDialogLayout(
    PalNativeUiDialogLayout *out);

bool PalNativeUi_Font10IdentityMatches(
    uint32_t glyph_count,
    uint32_t image_bytes,
    uint32_t payload_crc32,
    uint8_t cell_width,
    uint8_t cell_height,
    uint8_t ascent,
    uint8_t descent);

/* Draw a packed 10x10 FONT10 bitmap into an indexed destination. */
bool PalNativeUi_DrawFont10Glyph(
    const uint8_t *bitmap,
    uint8_t *pixels,
    uint16_t pitch,
    uint16_t surface_width,
    uint16_t surface_height,
    int16_t x,
    int16_t y,
    uint8_t color);

/* Map one legacy screen coordinate to the selected physical profile. */
int16_t PalNativeUi_MapVirtualX(int16_t x);
int16_t PalNativeUi_MapVirtualY(int16_t y);

/*
 * Map one RLE material from its legacy position directly into the native
 * indexed framebuffer. The source is sampled independently on each axis,
 * transparency is retained, and no intermediate bitmap is allocated.
 */
bool PalNativeUi_BlitRleMappedIndexed(
    const uint8_t *rle,
    size_t rle_bytes,
    uint8_t *pixels,
    uint16_t pitch,
    uint16_t surface_width,
    uint16_t surface_height,
    int16_t virtual_x,
    int16_t virtual_y,
    PalNativeUiRect *drawn);

/*
 * Decode only the nearest-centre samples needed for one bounded sprite.
 * This is the permitted per-asset downsample path: the PAL RLE source stays
 * read-only, transparency is retained, aspect ratio is preserved, and no
 * sprite-sized scratch buffer or heap is used.
 */
bool PalNativeUi_BlitRleFitIndexed(
    const uint8_t *rle,
    size_t rle_bytes,
    uint8_t *pixels,
    uint16_t pitch,
    uint16_t surface_width,
    uint16_t surface_height,
    PalNativeUiRect box,
    PalNativeUiRect *drawn);

#ifdef __cplusplus
}
#endif

#endif
