#ifndef PAL_FONT_CACHE_H
#define PAL_FONT_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_FONT_MAGIC 0x544e4650u
#define PAL_FONT_VERSION 1u
#define PAL_FONT_HEADER_SIZE 32u
#define PAL_FONT_GLYPH_BYTES 32u

typedef struct PalFontCache {
    const uint8_t *base;
    uint32_t size;
    uint16_t glyph_count;
    uint16_t glyph_bytes;
    uint32_t codepoint_table_offset;
    uint32_t glyph_data_offset;
    uint32_t glyph_data_size;
} PalFontCache;

bool PalFont_Open(const PalPack *nor_pack, PalFontCache *cache);
bool PalFont_FindGlyph(const PalFontCache *cache, uint16_t codepoint, const uint8_t **glyph, uint16_t *glyph_bytes);

#ifdef __cplusplus
}
#endif

#endif
