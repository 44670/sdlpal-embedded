#ifndef PAL_DIALOG_STATIC_H
#define PAL_DIALOG_STATIC_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_DIALOG_ICON_CHUNK 12u
#define PAL_DIALOG_ICON_BYTES 282u
#define PAL_DIALOG_FACE_MAX_BYTES 64000u

typedef struct PalDialogAsset {
    const uint8_t *data;
    uint32_t size;
} PalDialogAsset;

bool PalDialog_MapIcons(const PalPack *nor_pack, PalDialogAsset *asset);
bool PalDialog_MapFace(const PalPack *nor_pack, uint16_t face_num, PalDialogAsset *asset);

#ifdef __cplusplus
}
#endif

#endif
