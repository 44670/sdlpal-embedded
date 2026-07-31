#ifndef PAL_FONT10_CACHE_H
#define PAL_FONT10_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_FONT10_VERSION 1u
#define PAL_FONT10_HEADER_BYTES 32u
#define PAL_FONT10_RECORD_BYTES 16u
#define PAL_FONT10_CELL_WIDTH 10u
#define PAL_FONT10_CELL_HEIGHT 10u
#define PAL_FONT10_BITMAP_BYTES 13u
#define PAL_FONT10_MAX_BYTES 65536u

typedef struct PalFont10Cache {
    const uint8_t *base;
    const uint8_t *records;
    uint32_t size;
    uint32_t glyph_count;
    uint32_t payload_crc32;
    uint16_t record_bytes;
    uint8_t cell_width;
    uint8_t cell_height;
    uint8_t ascent;
    uint8_t descent;
} PalFont10Cache;

typedef struct PalFont10Glyph {
    const uint8_t *bitmap;
    uint16_t codepoint;
    uint8_t advance;
} PalFont10Glyph;

/*
 * Open the host-generated FONT archive chunk 1 as a read-only view.
 * This validates the complete FONT10 header and every fixed-size record.
 */
bool PalFont10_Open(const PalPack *nor_pack, PalFont10Cache *cache);

/* Binary-search the sorted records without allocating or rasterizing. */
bool PalFont10_FindGlyph(
    const PalFont10Cache *cache,
    uint16_t codepoint,
    PalFont10Glyph *glyph);

#ifdef __cplusplus
}
#endif

#endif
