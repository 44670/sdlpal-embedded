#include "pal_ui_cache.h"

#include "pal_memory.h"

#include <stddef.h>

static bool map_native_asset(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalUiAsset *asset)
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

bool PalUi_MapUiSprite(const PalPack *nor_pack, PalUiAsset *asset)
{
    return map_native_asset(nor_pack, PAL_PACK_ARCHIVE_DATA, PAL_UI_SPRITE_CHUNK, asset);
}

bool PalUi_MapBattleEffect(const PalPack *nor_pack, PalUiAsset *asset)
{
    return map_native_asset(nor_pack, PAL_PACK_ARCHIVE_DATA, PAL_UI_BATTLE_EFFECT_CHUNK, asset);
}

bool PalUi_MapItemBitmap(const PalPack *nor_pack, uint16_t bitmap_num, PalUiAsset *asset)
{
    return map_native_asset(nor_pack, PAL_PACK_ARCHIVE_BALL, bitmap_num, asset);
}

bool PalUi_MapFaceBitmap(const PalPack *nor_pack, uint16_t face_num, PalUiAsset *asset)
{
    return map_native_asset(nor_pack, PAL_PACK_ARCHIVE_RGM, face_num, asset);
}

bool PalUi_LoadPaletteRgb(const PalPack *nor_pack, uint16_t palette_num, bool night, const uint8_t **rgb, uint32_t *byte_size)
{
    PalPackSpan span;
    uint32_t source_offset = 0;
    uint32_t i;

    if (rgb == NULL || byte_size == NULL) {
        return false;
    }
    *rgb = NULL;
    *byte_size = 0;

    if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_PAT, palette_num, &span)) {
        return false;
    }
    if (span.data == NULL || span.size < PAL_UI_PALETTE_RGB_BYTES || span.format != PAL_PACK_FORMAT_NATIVE) {
        return false;
    }
    if (night && span.size >= PAL_UI_PALETTE_RGB_BYTES * 2u) {
        source_offset = PAL_UI_PALETTE_RGB_BYTES;
    }

    for (i = 0; i < PAL_UI_PALETTE_RGB_BYTES; i++) {
        uint8_t color = span.data[source_offset + i];
        pal_sram_misc[i] = (uint8_t)(color << 2);
    }

    *rgb = pal_sram_misc;
    *byte_size = PAL_UI_PALETTE_RGB_BYTES;
    return true;
}
