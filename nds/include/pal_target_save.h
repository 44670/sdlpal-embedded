#ifndef PAL_NDS_TARGET_SAVE_H
#define PAL_NDS_TARGET_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PalTargetSave_Init(void);
void NdsTargetSave_SetDldiReady(bool ready);
void NdsTargetSave_SetRetailReady(bool ready);
bool PalTargetSave_Available(void);
int PalTargetSave_Type(void);
uint32_t PalTargetSave_Capacity(void);
bool PalTargetSave_ProbeSlot(int slot, uint16_t *saved_times);
bool PalTargetSave_ReadSlot(
   int slot, void *destination, size_t capacity, size_t *out_size);
bool PalTargetSave_WriteSlot(int slot, const void *source, size_t size);

#ifdef __cplusplus
}
#endif

#endif
