#ifndef PAL_SFX_CACHE_H
#define PAL_SFX_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_SFX_MAX_BANKED_SOUNDS 32u

typedef struct PalSfxEntry {
    uint32_t offset;
    uint32_t size;
    uint16_t chunk_num;
} PalSfxEntry;

typedef struct PalSfxBank {
    uint16_t entry_count;
    uint32_t used_bytes;
    const PalSfxEntry *entries;
    const uint8_t *data;
} PalSfxBank;

bool PalSfx_LoadBank(const PalPack *tf_pack, const uint16_t *chunk_nums, uint16_t count, PalSfxBank *sfx_bank);
bool PalSfx_LoadBankReadAt(
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const uint16_t *chunk_nums,
    uint16_t count,
    PalSfxBank *sfx_bank);
bool PalSfx_Get(const PalSfxBank *sfx_bank, uint16_t chunk_num, const uint8_t **data, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
