#ifndef PAL_UI_CACHE_H
#define PAL_UI_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_UI_SPRITE_CHUNK 9u
#define PAL_UI_BATTLE_EFFECT_CHUNK 10u
#define PAL_UI_PALETTE_COLORS 256u
#define PAL_UI_PALETTE_RGB_BYTES (PAL_UI_PALETTE_COLORS * 3u)

typedef struct PalUiAsset {
    const uint8_t *data;
    uint32_t size;
} PalUiAsset;

bool PalUi_MapUiSprite(const PalPack *nor_pack, PalUiAsset *asset);
bool PalUi_MapBattleEffect(const PalPack *nor_pack, PalUiAsset *asset);
bool PalUi_MapItemBitmap(const PalPack *nor_pack, uint16_t bitmap_num, PalUiAsset *asset);
bool PalUi_MapFaceBitmap(const PalPack *nor_pack, uint16_t face_num, PalUiAsset *asset);
bool PalUi_LoadPaletteRgb(const PalPack *nor_pack, uint16_t palette_num, bool night, const uint8_t **rgb, uint32_t *byte_size);

#ifdef __cplusplus
}
#endif

#endif
