#ifndef PAL_CORES3SE_ENGINE_PACK_PROVIDER_H
#define PAL_CORES3SE_ENGINE_PACK_PROVIDER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../../embedded/pal_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*PalEngineBridgeReadAt)(void *user, uint32_t offset, uint8_t *dst, uint32_t size);

bool PalEngineBridge_SetNorPackConst(const uint8_t *image, uint32_t image_size);
bool PalEngineBridge_SetTfPackReadAt(uint32_t pack_size, PalEngineBridgeReadAt read_at, void *user);
void PalEngineBridge_ClearPacks(void);
bool PalEngineBridge_TargetInitPacks(void);
bool PalContract_TargetOpenNorPack(PalPack *pack);
bool PalContract_TargetOpenTfPack(PalPack *pack);
bool PalEngineBridge_IsPackFile(FILE *fp);
#if defined(PAL_CARDPUTER_EXTREME)
int PalEngineBridge_ReadNativeRngFrame(uint16_t movie_id,
                                      uint16_t frame_id,
                                      uint8_t *dst,
                                      uint32_t dst_capacity);
#endif

#ifdef __cplusplus
}
#endif

#endif
