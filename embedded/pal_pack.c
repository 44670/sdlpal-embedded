#include "pal_pack.h"

#include <string.h>

#define PAL_PACK_HEADER_SIZE 32u
#define PAL_PACK_ARCHIVE_ENTRY_SIZE 12u
#define PAL_PACK_CHUNK_ENTRY_SIZE 16u

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

bool PalPack_OpenConst(PalPack *pack, const uint8_t *data, uint32_t size)
{
    uint16_t version;
    uint16_t header_size;
    uint16_t archive_count;
    uint32_t archive_table_offset;
    uint32_t pack_size;

    if (pack == NULL || data == NULL || size < PAL_PACK_HEADER_SIZE) {
        return false;
    }

    if (read_le32(data) != PAL_PACK_MAGIC) {
        return false;
    }

    version = read_le16(data + 4);
    header_size = read_le16(data + 6);
    archive_count = read_le16(data + 8);
    archive_table_offset = read_le32(data + 12);
    pack_size = read_le32(data + 24);

    if (version != PAL_PACK_VERSION || header_size != PAL_PACK_HEADER_SIZE) {
        return false;
    }
    if (pack_size != size) {
        return false;
    }
    if (!checked_range(archive_table_offset, (uint32_t)archive_count * PAL_PACK_ARCHIVE_ENTRY_SIZE, size)) {
        return false;
    }

    pack->base = data;
    pack->size = size;
    pack->archive_count = archive_count;
    pack->archive_table_offset = archive_table_offset;
    return true;
}

static bool find_archive(const PalPack *pack, uint16_t archive_id, const uint8_t **entry)
{
    uint16_t i;

    if (pack == NULL || pack->base == NULL || entry == NULL) {
        return false;
    }

    for (i = 0; i < pack->archive_count; i++) {
        const uint8_t *cur = pack->base + pack->archive_table_offset + (uint32_t)i * PAL_PACK_ARCHIVE_ENTRY_SIZE;
        if (read_le16(cur) == archive_id) {
            *entry = cur;
            return true;
        }
    }
    return false;
}

bool PalPack_GetChunkCount(const PalPack *pack, uint16_t archive_id, uint16_t *chunk_count)
{
    const uint8_t *archive;

    if (chunk_count == NULL || !find_archive(pack, archive_id, &archive)) {
        return false;
    }

    *chunk_count = read_le16(archive + 2);
    return true;
}

bool PalPack_MapConst(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalPackSpan *span)
{
    const uint8_t *archive;
    const uint8_t *chunk;
    uint16_t chunk_count;
    uint32_t chunk_table_offset;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint16_t format;
    uint16_t flags;

    if (span == NULL || !find_archive(pack, archive_id, &archive)) {
        return false;
    }

    chunk_count = read_le16(archive + 2);
    chunk_table_offset = read_le32(archive + 4);
    if (chunk_id >= chunk_count) {
        return false;
    }
    if (!checked_range(chunk_table_offset, (uint32_t)chunk_count * PAL_PACK_CHUNK_ENTRY_SIZE, pack->size)) {
        return false;
    }

    chunk = pack->base + chunk_table_offset + (uint32_t)chunk_id * PAL_PACK_CHUNK_ENTRY_SIZE;
    payload_offset = read_le32(chunk);
    payload_size = read_le32(chunk + 4);
    format = read_le16(chunk + 8);
    flags = read_le16(chunk + 10);

    if ((flags & PAL_PACK_CHUNK_F_COMPRESSED) != 0) {
        return false;
    }
    if (!checked_range(payload_offset, payload_size, pack->size)) {
        return false;
    }

    span->data = pack->base + payload_offset;
    span->size = payload_size;
    span->format = format;
    span->flags = flags;
    return true;
}

bool PalPack_CopyRaw(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, uint8_t *dst, uint32_t dst_capacity, uint32_t *out_size)
{
    PalPackSpan span;

    if (!PalPack_MapConst(pack, archive_id, chunk_id, &span)) {
        return false;
    }
    if (span.size > dst_capacity || (span.size != 0 && dst == NULL)) {
        return false;
    }

    if (span.size != 0) {
        memcpy(dst, span.data, span.size);
    }
    if (out_size != NULL) {
        *out_size = span.size;
    }
    return true;
}
