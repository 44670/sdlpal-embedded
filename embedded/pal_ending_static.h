#ifndef PAL_ENDING_STATIC_H
#define PAL_ENDING_STATIC_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_ENDING_FBP_BYTES (320u * 200u)

typedef struct PalEndingBuffer {
    uint8_t *data;
    uint32_t size;
} PalEndingBuffer;

typedef struct PalEndingConstAsset {
    const uint8_t *data;
    uint32_t size;
} PalEndingConstAsset;

typedef struct PalEndingScreenPair {
    PalEndingBuffer upper;
    PalEndingBuffer lower;
} PalEndingScreenPair;

bool PalEnding_LoadFbp(const PalPack *tf_pack, uint16_t fbp_num, PalEndingBuffer *buffer);
bool PalEnding_LoadFbpPair(const PalPack *tf_pack, uint16_t upper_fbp_num, uint16_t lower_fbp_num, PalEndingScreenPair *pair);
bool PalEnding_MapSprite(const PalPack *nor_pack, uint16_t mgo_num, PalEndingConstAsset *asset);

#ifdef __cplusplus
}
#endif

#endif
