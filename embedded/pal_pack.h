#ifndef PAL_PACK_H
#define PAL_PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_PACK_MAGIC 0x4b504c50u
#define PAL_PACK_VERSION 1u

enum PalPackArchiveId {
    PAL_PACK_ARCHIVE_ABC = 1,
    PAL_PACK_ARCHIVE_BALL = 2,
    PAL_PACK_ARCHIVE_DATA = 3,
    PAL_PACK_ARCHIVE_F = 4,
    PAL_PACK_ARCHIVE_FBP = 5,
    PAL_PACK_ARCHIVE_FIRE = 6,
    PAL_PACK_ARCHIVE_GOP = 7,
    PAL_PACK_ARCHIVE_MAP = 8,
    PAL_PACK_ARCHIVE_MGO = 9,
    PAL_PACK_ARCHIVE_MIDI = 10,
    PAL_PACK_ARCHIVE_MUS = 11,
    PAL_PACK_ARCHIVE_PAT = 12,
    PAL_PACK_ARCHIVE_RGM = 13,
    PAL_PACK_ARCHIVE_RNG = 14,
    PAL_PACK_ARCHIVE_SSS = 15,
    PAL_PACK_ARCHIVE_VOC = 16,
    PAL_PACK_ARCHIVE_TEXT = 17,
    PAL_PACK_ARCHIVE_FONT = 18,
    PAL_PACK_ARCHIVE_SFX = 19,
    PAL_PACK_ARCHIVE_CACHE = 20,
};

enum PalPackFormat {
    PAL_PACK_FORMAT_RAW = 0,
    PAL_PACK_FORMAT_NATIVE = 1,
    PAL_PACK_FORMAT_RNG_FRAMES = 2,
    PAL_PACK_FORMAT_TEXT_UTF16 = 3,
    PAL_PACK_FORMAT_FONT_GLYPHS = 4,
    PAL_PACK_FORMAT_SFX_PCM16 = 5,
    PAL_PACK_FORMAT_FONT10 = 6,
};

#define PAL_PACK_CHUNK_F_COMPRESSED 0x0001u

typedef struct PalPack {
    const uint8_t *base;
    uint32_t size;
    uint16_t archive_count;
    uint32_t archive_table_offset;
} PalPack;

typedef struct PalPackSpan {
    const uint8_t *data;
    uint32_t size;
    uint16_t format;
    uint16_t flags;
} PalPackSpan;

typedef struct PalPackToc {
    const uint8_t *base;
    uint32_t toc_size;
    uint32_t pack_size;
    uint16_t archive_count;
    uint32_t archive_table_offset;
} PalPackToc;

typedef struct PalPackChunkInfo {
    uint32_t offset;
    uint32_t size;
    uint16_t format;
    uint16_t flags;
} PalPackChunkInfo;

typedef bool (*PalPackReadAt)(void *user, uint32_t offset, uint8_t *dst, uint32_t size);

bool PalPack_OpenConst(PalPack *pack, const uint8_t *data, uint32_t size);
bool PalPack_GetChunkCount(const PalPack *pack, uint16_t archive_id, uint16_t *chunk_count);
bool PalPack_MapConst(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalPackSpan *span);
bool PalPack_CopyRaw(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, uint8_t *dst, uint32_t dst_capacity, uint32_t *out_size);
bool PalPack_OpenTocCopy(PalPackToc *toc, const uint8_t *pack_image, uint32_t pack_size, uint8_t *toc_buffer, uint32_t toc_capacity);
bool PalPack_OpenTocRead(PalPackToc *toc, PalPackReadAt read_at, void *user, uint32_t pack_size, uint8_t *toc_buffer, uint32_t toc_capacity);
bool PalPackToc_GetChunkCount(const PalPackToc *toc, uint16_t archive_id, uint16_t *chunk_count);
bool PalPackToc_GetChunkInfo(const PalPackToc *toc, uint16_t archive_id, uint16_t chunk_id, PalPackChunkInfo *info);
bool PalPackToc_CopyRawFromImage(const PalPackToc *toc, const uint8_t *pack_image, uint16_t archive_id, uint16_t chunk_id, uint8_t *dst, uint32_t dst_capacity, uint32_t *out_size);
bool PalPackToc_CopyRawReadAt(const PalPackToc *toc, PalPackReadAt read_at, void *user, uint16_t archive_id, uint16_t chunk_id, uint8_t *dst, uint32_t dst_capacity, uint32_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
