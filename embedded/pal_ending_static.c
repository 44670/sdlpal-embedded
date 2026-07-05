#include "pal_ending_static.h"

#include "pal_memory.h"

#include <stddef.h>

static bool map_native_asset(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalPackSpan *span)
{
    if (span == NULL) {
        return false;
    }
    if (!PalPack_MapConst(pack, archive_id, chunk_id, span)) {
        return false;
    }
    return span->data != NULL && span->size != 0 && span->format == PAL_PACK_FORMAT_NATIVE;
}

static bool load_fbp_to_buffer(const PalPack *tf_pack, uint16_t fbp_num, uint8_t *dst, PalEndingBuffer *buffer)
{
    PalPackSpan span;
    uint32_t copied = 0;

    if (buffer == NULL || dst == NULL) {
        return false;
    }
    buffer->data = NULL;
    buffer->size = 0;

    if (!map_native_asset(tf_pack, PAL_PACK_ARCHIVE_FBP, fbp_num, &span) || span.size != PAL_ENDING_FBP_BYTES) {
        return false;
    }
    if (!PalPack_CopyRaw(tf_pack, PAL_PACK_ARCHIVE_FBP, fbp_num, dst, PAL_PSRAM_ENDING_FBP_BYTES, &copied)) {
        return false;
    }
    if (copied != PAL_ENDING_FBP_BYTES) {
        return false;
    }

    buffer->data = dst;
    buffer->size = copied;
    return true;
}

bool PalEnding_LoadFbp(const PalPack *tf_pack, uint16_t fbp_num, PalEndingBuffer *buffer)
{
    return load_fbp_to_buffer(tf_pack, fbp_num, pal_psram_ending_fbp_a, buffer);
}

bool PalEnding_LoadFbpPair(const PalPack *tf_pack, uint16_t upper_fbp_num, uint16_t lower_fbp_num, PalEndingScreenPair *pair)
{
    if (pair == NULL) {
        return false;
    }
    pair->upper.data = NULL;
    pair->upper.size = 0;
    pair->lower.data = NULL;
    pair->lower.size = 0;

    return load_fbp_to_buffer(tf_pack, upper_fbp_num, pal_psram_ending_fbp_a, &pair->upper) &&
           load_fbp_to_buffer(tf_pack, lower_fbp_num, pal_psram_ending_fbp_b, &pair->lower);
}

bool PalEnding_MapSprite(const PalPack *nor_pack, uint16_t mgo_num, PalEndingConstAsset *asset)
{
    PalPackSpan span;

    if (asset == NULL) {
        return false;
    }
    asset->data = NULL;
    asset->size = 0;

    if (!map_native_asset(nor_pack, PAL_PACK_ARCHIVE_MGO, mgo_num, &span)) {
        return false;
    }

    asset->data = span.data;
    asset->size = span.size;
    return true;
}
