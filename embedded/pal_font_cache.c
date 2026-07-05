#include "pal_font_cache.h"

#include <stddef.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool checked_range(uint32_t offset, uint32_t length, uint32_t size)
{
    return offset <= size && length <= size - offset;
}

static bool validate_codepoints(const uint8_t *base, uint32_t table_offset, uint16_t count)
{
    uint16_t i;
    uint16_t prev = 0;

    for (i = 0; i < count; i++) {
        uint16_t cur = read_le16(base + table_offset + (uint32_t)i * 2u);
        if (i != 0 && cur <= prev) {
            return false;
        }
        prev = cur;
    }
    return true;
}

bool PalFont_Open(const PalPack *nor_pack, PalFontCache *cache)
{
    PalPackSpan span;
    uint16_t version;
    uint16_t header_size;
    uint16_t glyph_count;
    uint16_t glyph_bytes;
    uint32_t codepoint_table_offset;
    uint32_t glyph_data_offset;
    uint32_t glyph_data_size;

    if (cache == NULL) {
        return false;
    }
    cache->base = NULL;
    cache->size = 0;
    cache->glyph_count = 0;
    cache->glyph_bytes = 0;
    cache->codepoint_table_offset = 0;
    cache->glyph_data_offset = 0;
    cache->glyph_data_size = 0;

    if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_FONT, 0, &span)) {
        return false;
    }
    if (span.data == NULL || span.size < PAL_FONT_HEADER_SIZE || span.format != PAL_PACK_FORMAT_FONT_GLYPHS) {
        return false;
    }
    if (read_le32(span.data) != PAL_FONT_MAGIC) {
        return false;
    }

    version = read_le16(span.data + 4u);
    header_size = read_le16(span.data + 6u);
    glyph_count = read_le16(span.data + 8u);
    glyph_bytes = read_le16(span.data + 10u);
    codepoint_table_offset = read_le32(span.data + 12u);
    glyph_data_offset = read_le32(span.data + 16u);
    glyph_data_size = read_le32(span.data + 20u);

    if (version != PAL_FONT_VERSION || header_size != PAL_FONT_HEADER_SIZE || glyph_bytes != PAL_FONT_GLYPH_BYTES) {
        return false;
    }
    if (glyph_count == 0 || glyph_data_size != (uint32_t)glyph_count * glyph_bytes) {
        return false;
    }
    if (!checked_range(codepoint_table_offset, (uint32_t)glyph_count * 2u, span.size)) {
        return false;
    }
    if (!checked_range(glyph_data_offset, glyph_data_size, span.size)) {
        return false;
    }
    if (!validate_codepoints(span.data, codepoint_table_offset, glyph_count)) {
        return false;
    }

    cache->base = span.data;
    cache->size = span.size;
    cache->glyph_count = glyph_count;
    cache->glyph_bytes = glyph_bytes;
    cache->codepoint_table_offset = codepoint_table_offset;
    cache->glyph_data_offset = glyph_data_offset;
    cache->glyph_data_size = glyph_data_size;
    return true;
}

bool PalFont_FindGlyph(const PalFontCache *cache, uint16_t codepoint, const uint8_t **glyph, uint16_t *glyph_bytes)
{
    uint32_t low;
    uint32_t high;

    if (cache == NULL || cache->base == NULL || glyph == NULL || glyph_bytes == NULL) {
        return false;
    }

    low = 0;
    high = cache->glyph_count;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        uint16_t cur = read_le16(cache->base + cache->codepoint_table_offset + mid * 2u);
        if (cur < codepoint) {
            low = mid + 1u;
        } else if (cur > codepoint) {
            high = mid;
        } else {
            *glyph = cache->base + cache->glyph_data_offset + mid * cache->glyph_bytes;
            *glyph_bytes = cache->glyph_bytes;
            return true;
        }
    }

    *glyph = NULL;
    *glyph_bytes = 0;
    return false;
}
