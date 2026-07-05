#ifndef PAL_MENU_STATIC_H
#define PAL_MENU_STATIC_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_MENU_BACKGROUND_BYTES (320u * 200u)
#define PAL_MENU_IMAGE_BYTES 64000u
#define PAL_MENU_BOX_MAX_BYTES (72u * 72u)

typedef struct PalMenuBuffer {
    uint8_t *data;
    uint32_t size;
} PalMenuBuffer;

typedef struct PalMenuConstAsset {
    const uint8_t *data;
    uint32_t size;
} PalMenuConstAsset;

bool PalMenu_LoadBackground(const PalPack *tf_pack, uint16_t fbp_num, PalMenuBuffer *buffer);
bool PalMenu_LoadBackgroundReadAt(
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    uint16_t fbp_num,
    PalMenuBuffer *buffer);
bool PalMenu_CopyImage(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalMenuBuffer *buffer);
bool PalMenu_MapImage(const PalPack *pack, uint16_t archive_id, uint16_t chunk_id, PalMenuConstAsset *asset);
bool PalMenu_PrepareBox(uint16_t width, uint16_t height, uint8_t fill, PalMenuBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif
