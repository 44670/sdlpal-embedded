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
bool PalEngineBridge_SetCorePackConst(const uint8_t *image, uint32_t image_size);
bool PalEngineBridge_SetOverlayPackConst(const uint8_t *image, uint32_t image_size);
void PalEngineBridge_ClearOverlay(void);
void PalEngineBridge_ClearOverlayPack(void);
bool PalEngineBridge_SetTfPackReadAt(uint32_t pack_size, PalEngineBridgeReadAt read_at, void *user);
bool PalEngineBridge_GetActivePackSetId(uint32_t *pack_set_id);
bool PalEngineBridge_HasNativeChunk(uint16_t archive_id, uint16_t chunk_id);
bool PalEngineBridge_MapNativeChunk(
   uint16_t archive_id,
   uint16_t chunk_id,
   const uint8_t **data,
   uint32_t *size);
int PalEngineBridge_GetNativeChunkSize(FILE *fp, uint16_t chunk_id);
bool PalEngineBridge_ReadNativeChunkRange(FILE *fp,
                                          uint16_t chunk_id,
                                          uint32_t chunk_offset,
                                          uint8_t *dst,
                                          uint32_t size);
void PalEngineBridge_ClearPacks(void);
bool PalEngineBridge_TargetInitPacks(void);
bool PalContract_TargetOpenNorPack(PalPack *pack);
bool PalContract_TargetOpenTfPack(PalPack *pack);
bool PalEngineBridge_IsPackFile(FILE *fp);
#if defined(PAL_EXTREME_TWO_SCREENS)
typedef struct PalEngineBridgeNativeRngFrame {
   const uint8_t *mapped_data;
   uint32_t tf_offset;
   uint32_t size;
   bool tf_backed;
} PalEngineBridgeNativeRngFrame;

bool PalEngineBridge_OpenNativeRngFrame(
   uint16_t movie_id,
   uint16_t frame_id,
   PalEngineBridgeNativeRngFrame *frame);
bool PalEngineBridge_ReadNativeRngFrameRange(
   const PalEngineBridgeNativeRngFrame *frame,
   uint32_t frame_offset,
   uint8_t *dst,
   uint32_t size);
int PalEngineBridge_ReadNativeRngFrame(uint16_t movie_id,
                                      uint16_t frame_id,
                                      uint8_t *dst,
                                      uint32_t dst_capacity);
#endif

#ifdef __cplusplus
}
#endif

#endif
