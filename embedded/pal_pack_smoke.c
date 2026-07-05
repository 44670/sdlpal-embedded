#include "pal_pack.h"

#include <stdint.h>

static const uint8_t kSmokePack[] = {
    0x50, 0x4c, 0x50, 0x4b, 0x01, 0x00, 0x20, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x2c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xde, 0xad, 0xbe, 0xef,
};

static uint8_t g_copy_buffer[4];
static uint8_t g_toc_buffer[64];

static bool read_smoke_pack(void *user, uint32_t offset, uint8_t *dst, uint32_t size)
{
    const uint8_t *pack = (const uint8_t *)user;
    uint32_t i;

    if (offset > sizeof(kSmokePack) || size > sizeof(kSmokePack) - offset) {
        return false;
    }
    for (i = 0; i < size; i++) {
        dst[i] = pack[offset + i];
    }
    return true;
}

int main(void)
{
    PalPack pack;
    PalPackSpan span;
    PalPackToc toc;
    PalPackChunkInfo info;
    uint32_t copied = 0;
    uint16_t chunk_count = 0;

    if (!PalPack_OpenConst(&pack, kSmokePack, (uint32_t)sizeof(kSmokePack))) {
        return 1;
    }
    if (!PalPack_MapConst(&pack, PAL_PACK_ARCHIVE_DATA, 0, &span)) {
        return 2;
    }
    if (!PalPack_GetChunkCount(&pack, PAL_PACK_ARCHIVE_DATA, &chunk_count) || chunk_count != 1) {
        return 6;
    }
    if (span.size != 4 || span.data[0] != 0xde || span.data[3] != 0xef) {
        return 3;
    }
    if (!PalPack_CopyRaw(&pack, PAL_PACK_ARCHIVE_DATA, 0, g_copy_buffer, (uint32_t)sizeof(g_copy_buffer), &copied)) {
        return 4;
    }
    if (copied != 4 || g_copy_buffer[1] != 0xad || g_copy_buffer[2] != 0xbe) {
        return 5;
    }
    if (!PalPack_OpenTocCopy(&toc, kSmokePack, (uint32_t)sizeof(kSmokePack), g_toc_buffer, (uint32_t)sizeof(g_toc_buffer))) {
        return 7;
    }
    if (toc.base != g_toc_buffer || toc.toc_size != 64u || toc.pack_size != sizeof(kSmokePack)) {
        return 8;
    }
    if (!PalPackToc_GetChunkCount(&toc, PAL_PACK_ARCHIVE_DATA, &chunk_count) || chunk_count != 1) {
        return 9;
    }
    if (!PalPackToc_GetChunkInfo(&toc, PAL_PACK_ARCHIVE_DATA, 0, &info)) {
        return 10;
    }
    if (info.offset != 64u || info.size != 4u || info.format != PAL_PACK_FORMAT_RAW || info.flags != 0u) {
        return 11;
    }
    g_copy_buffer[0] = 0;
    g_copy_buffer[1] = 0;
    g_copy_buffer[2] = 0;
    g_copy_buffer[3] = 0;
    if (!PalPackToc_CopyRawFromImage(&toc, kSmokePack, PAL_PACK_ARCHIVE_DATA, 0, g_copy_buffer, (uint32_t)sizeof(g_copy_buffer), &copied)) {
        return 12;
    }
    if (copied != 4 || g_copy_buffer[0] != 0xde || g_copy_buffer[3] != 0xef) {
        return 13;
    }
    if (!PalPack_OpenTocRead(&toc, read_smoke_pack, (void *)kSmokePack, (uint32_t)sizeof(kSmokePack), g_toc_buffer, (uint32_t)sizeof(g_toc_buffer))) {
        return 14;
    }
    g_copy_buffer[0] = 0;
    g_copy_buffer[1] = 0;
    g_copy_buffer[2] = 0;
    g_copy_buffer[3] = 0;
    if (!PalPackToc_CopyRawReadAt(&toc, read_smoke_pack, (void *)kSmokePack, PAL_PACK_ARCHIVE_DATA, 0, g_copy_buffer, (uint32_t)sizeof(g_copy_buffer), &copied)) {
        return 15;
    }
    if (copied != 4 || g_copy_buffer[0] != 0xde || g_copy_buffer[3] != 0xef) {
        return 16;
    }
    return 0;
}
