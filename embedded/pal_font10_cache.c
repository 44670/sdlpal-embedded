#include "pal_font10_cache.h"

#include <stddef.h>

static const uint8_t kPalFont10Magic[8] = {
    'F', 'O', 'N', 'T', '1', '0', 0, 0
};

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

static bool bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    uint32_t size)
{
    uint32_t i;

    for (i = 0; i < size; i++) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

static uint32_t crc32_bytes(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xffffffffu;
    uint32_t i;

    for (i = 0; i < size; i++) {
        uint32_t bit;
        crc ^= data[i];
        for (bit = 0; bit < 8u; bit++) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static void clear_cache(PalFont10Cache *cache)
{
    cache->base = NULL;
    cache->records = NULL;
    cache->size = 0;
    cache->glyph_count = 0;
    cache->payload_crc32 = 0;
    cache->record_bytes = 0;
    cache->cell_width = 0;
    cache->cell_height = 0;
    cache->ascent = 0;
    cache->descent = 0;
}

static bool validate_records(const uint8_t *records, uint32_t glyph_count)
{
    uint32_t i;
    uint16_t previous = 0;

    for (i = 0; i < glyph_count; i++) {
        const uint8_t *record =
            records + i * PAL_FONT10_RECORD_BYTES;
        uint16_t codepoint = read_le16(record);

        if ((i != 0u && codepoint <= previous) ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
            return false;
        }
        if (record[2u] == 0u ||
            record[2u] > PAL_FONT10_CELL_WIDTH) {
            return false;
        }
        if ((record[PAL_FONT10_RECORD_BYTES - 1u] & 0x0fu) != 0u) {
            return false;
        }
        previous = codepoint;
    }
    return true;
}

bool PalFont10_Open(const PalPack *nor_pack, PalFont10Cache *cache)
{
    PalPackSpan span;
    const uint8_t *records;
    uint16_t version;
    uint16_t header_bytes;
    uint16_t record_bytes;
    uint16_t reserved;
    uint32_t glyph_count;
    uint32_t payload_bytes;
    uint32_t declared_crc32;
    uint32_t declared_bytes;
    uint8_t cell_width;
    uint8_t cell_height;
    uint8_t ascent;
    uint8_t descent;

    if (cache == NULL) {
        return false;
    }
    clear_cache(cache);

    if (!PalPack_MapConst(
            nor_pack,
            PAL_PACK_ARCHIVE_FONT,
            1u,
            &span)) {
        return false;
    }
    if (span.data == NULL ||
        span.size < PAL_FONT10_HEADER_BYTES ||
        span.size > PAL_FONT10_MAX_BYTES ||
        span.format != PAL_PACK_FORMAT_FONT10 ||
        span.flags != 0u) {
        return false;
    }
    if (!bytes_equal(span.data, kPalFont10Magic, 8u)) {
        return false;
    }

    version = read_le16(span.data + 8u);
    header_bytes = read_le16(span.data + 10u);
    glyph_count = read_le32(span.data + 12u);
    record_bytes = read_le16(span.data + 16u);
    cell_width = span.data[18u];
    cell_height = span.data[19u];
    ascent = span.data[20u];
    descent = span.data[21u];
    reserved = read_le16(span.data + 22u);
    declared_crc32 = read_le32(span.data + 24u);
    declared_bytes = read_le32(span.data + 28u);

    if (version != PAL_FONT10_VERSION ||
        header_bytes != PAL_FONT10_HEADER_BYTES ||
        record_bytes != PAL_FONT10_RECORD_BYTES ||
        cell_width != PAL_FONT10_CELL_WIDTH ||
        cell_height != PAL_FONT10_CELL_HEIGHT ||
        (uint16_t)ascent + (uint16_t)descent != cell_height ||
        reserved != 0u ||
        glyph_count == 0u) {
        return false;
    }
    if (glyph_count > (span.size - header_bytes) / record_bytes) {
        return false;
    }
    payload_bytes = glyph_count * (uint32_t)record_bytes;
    if (declared_bytes != span.size ||
        span.size != (uint32_t)header_bytes + payload_bytes) {
        return false;
    }

    records = span.data + header_bytes;
    if (crc32_bytes(records, payload_bytes) != declared_crc32 ||
        !validate_records(records, glyph_count)) {
        return false;
    }

    cache->base = span.data;
    cache->records = records;
    cache->size = span.size;
    cache->glyph_count = glyph_count;
    cache->payload_crc32 = declared_crc32;
    cache->record_bytes = record_bytes;
    cache->cell_width = cell_width;
    cache->cell_height = cell_height;
    cache->ascent = ascent;
    cache->descent = descent;
    return true;
}

bool PalFont10_FindGlyph(
    const PalFont10Cache *cache,
    uint16_t codepoint,
    PalFont10Glyph *glyph)
{
    uint32_t low;
    uint32_t high;

    if (glyph == NULL) {
        return false;
    }
    glyph->bitmap = NULL;
    glyph->codepoint = 0;
    glyph->advance = 0;

    if (cache == NULL ||
        cache->base == NULL ||
        cache->records == NULL ||
        cache->record_bytes != PAL_FONT10_RECORD_BYTES) {
        return false;
    }

    low = 0;
    high = cache->glyph_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const uint8_t *record =
            cache->records + middle * PAL_FONT10_RECORD_BYTES;
        uint16_t current = read_le16(record);

        if (current < codepoint) {
            low = middle + 1u;
        } else if (current > codepoint) {
            high = middle;
        } else {
            glyph->bitmap = record + 3u;
            glyph->codepoint = current;
            glyph->advance = record[2u];
            return true;
        }
    }
    return false;
}
