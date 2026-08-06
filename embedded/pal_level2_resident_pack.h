#ifndef PAL_LEVEL2_RESIDENT_PACK_H
#define PAL_LEVEL2_RESIDENT_PACK_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*PalLevel2ResidentReadAt)(
   void *user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size);

#define PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(id) (UINT32_C(1) << (id))
#define PAL_LEVEL2_RESIDENT_DEFAULT_MASK ( \
   PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_DATA) | \
   PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_MUS) | \
   PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_PAT) | \
   PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_RGM) | \
   PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_SSS) | \
   PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_TEXT) | \
   PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(PAL_PACK_ARCHIVE_FONT))

bool PalLevel2ResidentPack_Build(
   const PalPackToc *full_toc,
   uint32_t archive_mask,
   PalLevel2ResidentReadAt read_at,
   void *read_user,
   uint8_t *image,
   uint32_t image_capacity,
   uint32_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
