#include "pal_menu_static.h"

#include "pal_memory.h"

#include <stddef.h>
#include <string.h>

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

static bool get_native_toc_asset(const PalPackToc *toc, uint16_t archive_id, uint16_t chunk_id, PalPackChunkInfo *info)
{
    if (info == NULL) {
        return false;
    }
    if (!PalPackToc_GetChunkInfo(toc, archive_id, chunk_id, info)) {
        return false;
    }
    return info->size != 0 && info->format == PAL_PACK_FORMAT_NATIVE && info->flags == 0u;
}

bool PalMenu_LoadBackground(const PalPack *tf_pack, uint16_t fbp_num, PalMenuBuffer *buffer)
{
    PalPackSpan span;
    uint32_t copied = 0;

    if (buffer == NULL) {
        return false;
    }
    buffer->data = NULL;
    buffer->size = 0;

    if (!map_native_asset(tf_pack, PAL_PACK_ARCHIVE_FBP, fbp_num, &span) || span.size != PAL_MENU_BACKGROUND_BYTES) {
        return false;
    }
    if (!PalPack_CopyRaw(tf_pack, PAL_PACK_ARCHIVE_FBP, fbp_num, pal_psram_menu_background, PAL_PSRAM_MENU_BACKGROUND_BYTES, &copied)) {
        return false;
    }
    if (copied != PAL_MENU_BACKGROUND_BYTES) {
        return false;
    }

    buffer->data = pal_psram_menu_background;
    buffer->size = copied;
    return true;
}

bool PalMenu_LoadBackgroundReadAt(
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    uint16_t fbp_num,
    PalMenuBuffer *buffer)
{
    PalPackChunkInfo info;
    uint32_t copied = 0;

    if (buffer == NULL || read_at == NULL) {
        return false;
    }
    buffer->data = NULL;
    buffer->size = 0;

    if (!get_native_toc_asset(tf_toc, PAL_PACK_ARCHIVE_FBP, fbp_num, &info) || info.size != PAL_MENU_BACKGROUND_BYTES) {
        return false;
    }
    if (!PalPackToc_CopyRawReadAt(tf_toc, read_at, user, PAL_PACK_ARCHIVE_FBP, fbp_num, pal_psram_menu_background, PAL_PSRAM_MENU_BACKGROUND_BYTES, &copied)) {
        return false;
    }
    if (copied != PAL_MENU_BACKGROUND_BYTES) {
        return false;
    }

    buffer->data = pal_psram_menu_background;
    buffer->size = copied;
    return true;
}

bool PalMenu_CopyImage(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalMenuBuffer *buffer)
{
    PalPackSpan span;
    uint32_t copied = 0;

    if (buffer == NULL) {
        return false;
    }
    buffer->data = NULL;
    buffer->size = 0;

    if (!map_native_asset(pack, archive_id, chunk_id, &span) || span.size > PAL_PSRAM_MENU_IMAGE_BYTES) {
        return false;
    }
    if (!PalPack_CopyRaw(pack, archive_id, chunk_id, pal_psram_menu_image, PAL_PSRAM_MENU_IMAGE_BYTES, &copied)) {
        return false;
    }
    if (copied != span.size) {
        return false;
    }

    buffer->data = pal_psram_menu_image;
    buffer->size = copied;
    return true;
}

bool PalMenu_MapImage(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalMenuConstAsset *asset)
{
    PalPackSpan span;

    if (asset == NULL) {
        return false;
    }
    asset->data = NULL;
    asset->size = 0;

    if (!map_native_asset(pack, archive_id, chunk_id, &span)) {
        return false;
    }

    asset->data = span.data;
    asset->size = span.size;
    return true;
}

bool PalMenu_PrepareBox(uint16_t width, uint16_t height, uint8_t fill, PalMenuBuffer *buffer)
{
    uint32_t size = (uint32_t)width * height;

    if (buffer == NULL || width == 0 || height == 0 || size > PAL_PSRAM_MENU_BOX_BYTES) {
        return false;
    }
    memset(pal_psram_menu_box, fill, size);
    buffer->data = pal_psram_menu_box;
    buffer->size = size;
    return true;
}
