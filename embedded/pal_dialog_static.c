#include "pal_dialog_static.h"

#include <stddef.h>

static bool map_native_asset(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalDialogAsset *asset)
{
    PalPackSpan span;

    if (asset == NULL) {
        return false;
    }
    asset->data = NULL;
    asset->size = 0;

    if (!PalPack_MapConst(pack, archive_id, chunk_id, &span)) {
        return false;
    }
    if (span.data == NULL || span.size == 0 || span.format != PAL_PACK_FORMAT_NATIVE) {
        return false;
    }

    asset->data = span.data;
    asset->size = span.size;
    return true;
}

bool PalDialog_MapIcons(const PalPack *nor_pack, PalDialogAsset *asset)
{
    if (!map_native_asset(nor_pack, PAL_PACK_ARCHIVE_DATA, PAL_DIALOG_ICON_CHUNK, asset)) {
        return false;
    }
    return asset->size == PAL_DIALOG_ICON_BYTES;
}

bool PalDialog_MapFace(const PalPack *nor_pack, uint16_t face_num, PalDialogAsset *asset)
{
    if (!map_native_asset(nor_pack, PAL_PACK_ARCHIVE_RGM, face_num, asset)) {
        return false;
    }
    return asset->size <= PAL_DIALOG_FACE_MAX_BYTES;
}
