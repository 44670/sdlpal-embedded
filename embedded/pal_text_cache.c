#include "pal_text_cache.h"

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

static bool validate_offsets(const uint8_t *base, uint32_t table_offset, uint32_t count, uint32_t text_size)
{
    uint32_t i;
    uint32_t prev = 0;

    for (i = 0; i <= count; i++) {
        uint32_t cur = read_le32(base + table_offset + i * 4u);
        if (cur < prev || cur > text_size || (cur & 1u) != 0) {
            return false;
        }
        prev = cur;
    }
    return true;
}

static bool get_entry(
    const PalTextCache *cache,
    uint32_t table_offset,
    uint16_t index,
    uint16_t count,
    const uint8_t **utf16le,
    uint32_t *byte_size)
{
    uint32_t start;
    uint32_t end;

    if (cache == NULL || cache->base == NULL || utf16le == NULL || byte_size == NULL || index >= count) {
        return false;
    }

    start = read_le32(cache->base + table_offset + (uint32_t)index * 4u);
    end = read_le32(cache->base + table_offset + ((uint32_t)index + 1u) * 4u);
    if (start > end || end > cache->text_size || (start & 1u) != 0 || (end & 1u) != 0) {
        return false;
    }

    *utf16le = cache->base + cache->text_offset + start;
    *byte_size = end - start;
    return true;
}

bool PalText_Open(const PalPack *nor_pack, PalTextCache *cache)
{
    PalPackSpan span;
    uint16_t version;
    uint16_t header_size;
    uint16_t word_count;
    uint16_t message_count;
    uint32_t word_table_offset;
    uint32_t message_table_offset;
    uint32_t text_offset;
    uint32_t text_size;

    if (cache == NULL) {
        return false;
    }
    cache->base = NULL;
    cache->size = 0;
    cache->word_count = 0;
    cache->message_count = 0;
    cache->word_table_offset = 0;
    cache->message_table_offset = 0;
    cache->text_offset = 0;
    cache->text_size = 0;

    if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_TEXT, 0, &span)) {
        return false;
    }
    if (span.data == NULL || span.size < PAL_TEXT_HEADER_SIZE || span.format != PAL_PACK_FORMAT_TEXT_UTF16) {
        return false;
    }
    if (read_le32(span.data) != PAL_TEXT_MAGIC) {
        return false;
    }

    version = read_le16(span.data + 4u);
    header_size = read_le16(span.data + 6u);
    word_count = read_le16(span.data + 8u);
    message_count = read_le16(span.data + 10u);
    word_table_offset = read_le32(span.data + 12u);
    message_table_offset = read_le32(span.data + 16u);
    text_offset = read_le32(span.data + 20u);
    text_size = read_le32(span.data + 24u);

    if (version != PAL_TEXT_VERSION || header_size != PAL_TEXT_HEADER_SIZE) {
        return false;
    }
    if (!checked_range(word_table_offset, ((uint32_t)word_count + 1u) * 4u, span.size)) {
        return false;
    }
    if (!checked_range(message_table_offset, ((uint32_t)message_count + 1u) * 4u, span.size)) {
        return false;
    }
    if (!checked_range(text_offset, text_size, span.size) || (text_size & 1u) != 0) {
        return false;
    }
    if (!validate_offsets(span.data, word_table_offset, word_count, text_size)) {
        return false;
    }
    if (!validate_offsets(span.data, message_table_offset, message_count, text_size)) {
        return false;
    }

    cache->base = span.data;
    cache->size = span.size;
    cache->word_count = word_count;
    cache->message_count = message_count;
    cache->word_table_offset = word_table_offset;
    cache->message_table_offset = message_table_offset;
    cache->text_offset = text_offset;
    cache->text_size = text_size;
    return true;
}

bool PalText_GetWord(const PalTextCache *cache, uint16_t index, const uint8_t **utf16le, uint32_t *byte_size)
{
    return get_entry(cache, cache != NULL ? cache->word_table_offset : 0, index, cache != NULL ? cache->word_count : 0, utf16le, byte_size);
}

bool PalText_GetMessage(const PalTextCache *cache, uint16_t index, const uint8_t **utf16le, uint32_t *byte_size)
{
    return get_entry(cache, cache != NULL ? cache->message_table_offset : 0, index, cache != NULL ? cache->message_count : 0, utf16le, byte_size);
}
