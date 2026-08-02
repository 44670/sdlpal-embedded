#include "pal_engine_pack_provider.h"

#include "palcommon.h"
#include "pal_pack.h"
#include "pal_memory_profile.h"

#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#if defined(__has_include)
#if __has_include("esp_attr.h")
#include "esp_attr.h"
#endif
#endif

#define PAL_ENGINE_PACK_FILE_TAG  ((uintptr_t)0x504c0000u)
#define PAL_ENGINE_PACK_FILE_MASK ((uintptr_t)0xffff0000u)
#define PAL_ENGINE_PACK_FILE_LAST ((uintptr_t)0x504cffffu)
#define PAL_ENGINE_PACK_HEADER_SIZE 32u
#define PAL_ENGINE_PACK_SET_ID_OFFSET 20u
#define PAL_ENGINE_PACK_SIZE_OFFSET 24u
#define PAL_ENGINE_PACK_CRC32_OFFSET 28u
#define PAL_ENGINE_PACK_ARCHIVE_ENTRY_SIZE 12u
/*
 * A chapter-cache build already authenticates every NOR-backed core/overlay
 * payload with the SHA-256 stored in PALSET.BIN.  In that profile the pack
 * provider owns only structural/routing validation; repeating a whole-pack
 * CRC here would scan the same NOR bytes again and would also force a full
 * pal_tf.pak read at every boot.  Legacy Level1 layouts have no cache-level
 * SHA owner, so they retain strict provider CRC validation.
 */
#if defined(MEM_LEVEL1) && !defined(PAL_EXTREME_CHAPTER_CACHE)
#define PAL_ENGINE_STRICT_PACK_VALIDATION 1
#endif
#ifndef PAL_ENGINE_TF_TOC_BYTES
#define PAL_ENGINE_TF_TOC_BYTES (32u * 1024u)
#endif
#define PAL_ENGINE_LEVEL2_TF_MAP_BYTES (2176u * 1024u)
#if defined(PAL_STORAGE_SD_ONLY)
#define PAL_ENGINE_TF_MAP_BYTES PAL_MEM_LEVEL2_TRANSIENT_CHUNK_BYTES
#elif defined(MEM_LEVEL2)
#define PAL_ENGINE_TF_MAP_BYTES PAL_ENGINE_LEVEL2_TF_MAP_BYTES
#endif

#if defined(__GNUC__)
#if defined(MEM_LEVEL1)
#define PAL_ENGINE_PSRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#elif defined(EXT_RAM_BSS_ATTR)
#define PAL_ENGINE_PSRAM EXT_RAM_BSS_ATTR __attribute__((aligned(4)))
#else
#define PAL_ENGINE_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#endif
#else
#define PAL_ENGINE_PSRAM
#endif

typedef enum PalEngineArchiveStore {
   PAL_ENGINE_ARCHIVE_STORE_NONE = 0,
   PAL_ENGINE_ARCHIVE_STORE_CORE,
   PAL_ENGINE_ARCHIVE_STORE_OVERLAY,
   PAL_ENGINE_ARCHIVE_STORE_TF
} PalEngineArchiveStore;

static PalPack pal_engine_nor_pack;
static PalPack pal_engine_overlay_pack;
static PalPackToc pal_engine_tf_toc;
static PalEngineBridgeReadAt pal_engine_tf_read_at;
static void *pal_engine_tf_user;
static bool pal_engine_nor_ready;
static bool pal_engine_overlay_ready;
static bool pal_engine_tf_ready;
static uint32_t pal_engine_nor_set_id;
static uint32_t pal_engine_overlay_set_id;
static uint32_t pal_engine_tf_set_id;
static bool pal_engine_default_tried;
static bool pal_engine_tf_map_valid;
#if !defined(MEM_LEVEL1)
static uint16_t pal_engine_tf_map_archive;
static uint16_t pal_engine_tf_map_chunk;
static uint32_t pal_engine_tf_map_size;
#endif
#if defined(PAL_STORAGE_SD_ONLY)
#define PAL_ENGINE_TF_TOC_STORAGE pal_mem_level2_tf_toc
#define PAL_ENGINE_TF_MAP_STORAGE pal_mem_level2_transient_chunk
#elif defined(MEM_LEVEL1)
static uint8_t pal_sram_extreme_engine_tf_toc[PAL_ENGINE_TF_TOC_BYTES] PAL_ENGINE_PSRAM;
#define PAL_ENGINE_TF_TOC_STORAGE pal_sram_extreme_engine_tf_toc
#else
static uint8_t pal_psram_engine_tf_toc[PAL_ENGINE_TF_TOC_BYTES] PAL_ENGINE_PSRAM;
static uint8_t pal_psram_engine_tf_map[PAL_ENGINE_TF_MAP_BYTES] PAL_ENGINE_PSRAM;
#define PAL_ENGINE_TF_TOC_STORAGE pal_psram_engine_tf_toc
#define PAL_ENGINE_TF_MAP_STORAGE pal_psram_engine_tf_map
#endif

bool PalEngineBridge_LoadDefaultPacks(void) __attribute__((weak));
int __real_fclose(FILE *stream);

static uint32_t
read_le32(
   const uint8_t *p
)
{
   return (uint32_t)p[0] |
      ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) |
      ((uint32_t)p[3] << 24);
}

static uint16_t
read_le16(
   const uint8_t *p
)
{
   return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

#if defined(PAL_ENGINE_STRICT_PACK_VALIDATION)
static uint32_t
pack_crc32_update(
   uint32_t       crc,
   const uint8_t *bytes,
   uint32_t       offset,
   uint32_t       size
)
{
   static const uint32_t nibble_table[16] = {
      0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
      0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
      0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
      0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
   };
   uint32_t i;

   for (i = 0; i < size; i++)
   {
      uint32_t absolute = offset + i;
      crc ^= (absolute >= PAL_ENGINE_PACK_CRC32_OFFSET &&
         absolute < PAL_ENGINE_PACK_CRC32_OFFSET + 4u) ? 0u : bytes[i];
      crc = (crc >> 4) ^ nibble_table[crc & 0x0fu];
      crc = (crc >> 4) ^ nibble_table[crc & 0x0fu];
   }
   return crc;
}

static uint32_t
pack_crc32_const(
   const uint8_t *image,
   uint32_t       size
)
{
   return pack_crc32_update(0xffffffffu, image, 0, size) ^ 0xffffffffu;
}

static bool
pack_crc32_read_at(
   PalEngineBridgeReadAt read_at,
   void                 *user,
   uint32_t              size,
   uint8_t              *scratch,
   uint32_t              scratch_bytes,
   uint32_t             *out_crc
)
{
   uint32_t offset = 0;
   uint32_t crc = 0xffffffffu;

   if (read_at == NULL || scratch == NULL || scratch_bytes == 0 ||
      out_crc == NULL)
   {
      return false;
   }
   while (offset < size)
   {
      uint32_t amount = size - offset;
      if (amount > scratch_bytes)
      {
         amount = scratch_bytes;
      }
      if (!read_at(user, offset, scratch, amount))
      {
         return false;
      }
      crc = pack_crc32_update(crc, scratch, offset, amount);
      offset += amount;
   }
   *out_crc = crc ^ 0xffffffffu;
   return true;
}
#endif

static bool
const_packs_overlap(
   const PalPack *left,
   const PalPack *right
)
{
   uint16_t archive_index;

   if (left == NULL || right == NULL ||
      left->base == NULL || right->base == NULL)
   {
      return false;
   }

   for (archive_index = 0;
      archive_index < left->archive_count;
      archive_index++)
   {
      const uint8_t *archive = left->base + left->archive_table_offset +
         (uint32_t)archive_index * PAL_ENGINE_PACK_ARCHIVE_ENTRY_SIZE;
      uint16_t archive_id = read_le16(archive);
      uint16_t chunk_count;
      uint16_t chunk_id;

      if (!PalPack_GetChunkCount(left, archive_id, &chunk_count))
      {
         continue;
      }
      for (chunk_id = 0; chunk_id < chunk_count; chunk_id++)
      {
         PalPackSpan left_span;
         PalPackSpan right_span;

         if (PalPack_MapConst(left, archive_id, chunk_id, &left_span) &&
            left_span.size != 0 &&
            PalPack_MapConst(right, archive_id, chunk_id, &right_span) &&
            right_span.size != 0)
         {
            return true;
         }
      }
   }
   return false;
}

static bool
const_pack_toc_overlap(
   const PalPack *pack,
   const PalPackToc *toc
)
{
   uint16_t archive_index;

   if (pack == NULL || toc == NULL ||
      pack->base == NULL || toc->base == NULL)
   {
      return false;
   }

   for (archive_index = 0;
      archive_index < pack->archive_count;
      archive_index++)
   {
      const uint8_t *archive = pack->base + pack->archive_table_offset +
         (uint32_t)archive_index * PAL_ENGINE_PACK_ARCHIVE_ENTRY_SIZE;
      uint16_t archive_id = read_le16(archive);
      uint16_t chunk_count;
      uint16_t chunk_id;

      if (!PalPack_GetChunkCount(pack, archive_id, &chunk_count))
      {
         continue;
      }
      for (chunk_id = 0; chunk_id < chunk_count; chunk_id++)
      {
         PalPackSpan span;
         PalPackChunkInfo info;

         if (PalPack_MapConst(pack, archive_id, chunk_id, &span) &&
            span.size != 0 &&
            PalPackToc_GetChunkInfo(toc, archive_id, chunk_id, &info) &&
            info.size != 0)
         {
            return true;
         }
      }
   }
   return false;
}

static void
ensure_default_packs(
   void
)
{
   if (pal_engine_default_tried)
   {
      return;
   }
   pal_engine_default_tried = true;
   if (PalEngineBridge_LoadDefaultPacks != NULL)
   {
      (void)PalEngineBridge_LoadDefaultPacks();
      pal_engine_default_tried = true;
   }
}

static UINT
archive_from_file(
   FILE *fp
)
{
   uintptr_t value = (uintptr_t)fp;

   if (value < PAL_ENGINE_PACK_FILE_TAG || value > PAL_ENGINE_PACK_FILE_LAST ||
      (value & PAL_ENGINE_PACK_FILE_MASK) != PAL_ENGINE_PACK_FILE_TAG)
   {
      return 0;
   }

   return (UINT)(value & 0xffffu);
}

static PalEngineArchiveStore
find_archive_store(
   UINT archive_id,
   uint16_t *chunk_count
)
{
   uint16_t core_count = 0;
   uint16_t overlay_count = 0;
   uint16_t tf_count = 0;
   uint16_t max_count = 0;
   bool in_core;
   bool in_overlay;
   bool in_tf;

   if (archive_id == 0 || archive_id > 0xffffu)
   {
      return PAL_ENGINE_ARCHIVE_STORE_NONE;
   }

   in_overlay = pal_engine_overlay_ready &&
      PalPack_GetChunkCount(&pal_engine_overlay_pack,
         (uint16_t)archive_id, &overlay_count);
   in_core = pal_engine_nor_ready &&
      PalPack_GetChunkCount(&pal_engine_nor_pack,
         (uint16_t)archive_id, &core_count);
   in_tf = pal_engine_tf_ready &&
      PalPackToc_GetChunkCount(&pal_engine_tf_toc, (uint16_t)archive_id, &tf_count);

   if (!in_overlay && !in_core && !in_tf)
   {
      return PAL_ENGINE_ARCHIVE_STORE_NONE;
   }
   if (chunk_count != NULL)
   {
      if (in_overlay && overlay_count > max_count)
      {
         max_count = overlay_count;
      }
      if (in_core && core_count > max_count)
      {
         max_count = core_count;
      }
      if (in_tf && tf_count > max_count)
      {
         max_count = tf_count;
      }
      *chunk_count = max_count;
   }
   return in_overlay ? PAL_ENGINE_ARCHIVE_STORE_OVERLAY :
      (in_core ? PAL_ENGINE_ARCHIVE_STORE_CORE :
         PAL_ENGINE_ARCHIVE_STORE_TF);
}

static PalEngineArchiveStore
find_chunk_store(
   uint16_t archive_id,
   uint16_t chunk_id,
   PalPackSpan *const_span,
   PalPackChunkInfo *tf_info
)
{
   PalPackSpan core_span;
   PalPackSpan overlay_span;
   PalPackChunkInfo local_info;
   PalPackChunkInfo *info = tf_info != NULL ? tf_info : &local_info;
   bool in_overlay = pal_engine_overlay_ready &&
      PalPack_MapConst(&pal_engine_overlay_pack,
         archive_id, chunk_id, &overlay_span);
   bool in_core = pal_engine_nor_ready &&
      PalPack_MapConst(&pal_engine_nor_pack,
         archive_id, chunk_id, &core_span);
   bool in_tf = pal_engine_tf_ready &&
      PalPackToc_GetChunkInfo(&pal_engine_tf_toc, archive_id, chunk_id, info);
   unsigned int nonempty_count =
      (in_overlay && overlay_span.size != 0 ? 1u : 0u) +
      (in_core && core_span.size != 0 ? 1u : 0u) +
      (in_tf && info->size != 0 ? 1u : 0u);

   /*
    * Layout-v2 packs preserve source chunk numbers with zero-sized holes.
    * Normally one concrete non-empty chunk has exactly one owner.  The
    * SD-only Level2 profile is the narrow exception: its small resident cache
    * duplicates long-lived chunks from the portable complete TF pack, and
    * the resident copy wins.
    */
   if (nonempty_count > 1u)
   {
#if defined(PAL_STORAGE_SD_ONLY)
      if (nonempty_count != 2u ||
         !(in_core && core_span.size != 0) ||
         !(in_tf && info->size != 0) ||
         (in_overlay && overlay_span.size != 0))
      {
         return PAL_ENGINE_ARCHIVE_STORE_NONE;
      }
#else
      return PAL_ENGINE_ARCHIVE_STORE_NONE;
#endif
   }
   if (in_overlay && overlay_span.size != 0)
   {
      if (const_span != NULL)
      {
         *const_span = overlay_span;
      }
      return PAL_ENGINE_ARCHIVE_STORE_OVERLAY;
   }
   if (in_core && core_span.size != 0)
   {
      if (const_span != NULL)
      {
         *const_span = core_span;
      }
      return PAL_ENGINE_ARCHIVE_STORE_CORE;
   }
   if (in_tf && info->size != 0)
   {
      return PAL_ENGINE_ARCHIVE_STORE_TF;
   }
   if (in_overlay)
   {
      if (const_span != NULL)
      {
         *const_span = overlay_span;
      }
      return PAL_ENGINE_ARCHIVE_STORE_OVERLAY;
   }
   if (in_core)
   {
      if (const_span != NULL)
      {
         *const_span = core_span;
      }
      return PAL_ENGINE_ARCHIVE_STORE_CORE;
   }
   return in_tf ? PAL_ENGINE_ARCHIVE_STORE_TF :
      PAL_ENGINE_ARCHIVE_STORE_NONE;
}

bool
PalEngineBridge_SetNorPackConst(
   const uint8_t *image,
   uint32_t image_size
)
{
   PalPack candidate;
   uint32_t pack_size;
   uint32_t pack_set_id;
#if defined(PAL_ENGINE_STRICT_PACK_VALIDATION)
   uint32_t declared_crc;
#endif

   if (image == NULL || image_size < PAL_ENGINE_PACK_HEADER_SIZE)
   {
      return false;
   }

   pack_size = read_le32(image + PAL_ENGINE_PACK_SIZE_OFFSET);
   if (pack_size == 0 || pack_size > image_size)
   {
      return false;
   }
   pack_set_id = read_le32(image + PAL_ENGINE_PACK_SET_ID_OFFSET);
   if (pack_set_id == 0u)
   {
      return false;
   }
#if defined(PAL_ENGINE_STRICT_PACK_VALIDATION)
   declared_crc = read_le32(image + PAL_ENGINE_PACK_CRC32_OFFSET);
   if (declared_crc == 0 ||
      pack_crc32_const(image, pack_size) != declared_crc)
   {
      return false;
   }
#endif
   memset(&candidate, 0, sizeof(candidate));
   if (!PalPack_OpenConst(&candidate, image, pack_size))
   {
      return false;
   }
   if (pal_engine_overlay_ready &&
      (pack_set_id != pal_engine_overlay_set_id ||
         const_packs_overlap(&candidate, &pal_engine_overlay_pack)))
   {
      return false;
   }
   if (pal_engine_tf_ready &&
      (pack_set_id != pal_engine_tf_set_id
#if !defined(PAL_STORAGE_SD_ONLY)
         || const_pack_toc_overlap(&candidate, &pal_engine_tf_toc)
#endif
      ))
   {
      return false;
   }

   pal_engine_nor_pack = candidate;
   pal_engine_nor_set_id = pack_set_id;
   pal_engine_nor_ready = true;
   return true;
}

bool
PalEngineBridge_SetCorePackConst(
   const uint8_t *image,
   uint32_t image_size
)
{
   return PalEngineBridge_SetNorPackConst(image, image_size);
}

bool
PalEngineBridge_SetOverlayPackConst(
   const uint8_t *image,
   uint32_t image_size
)
{
   PalPack candidate;
   uint32_t pack_size;
   uint32_t pack_set_id;
#if defined(PAL_ENGINE_STRICT_PACK_VALIDATION)
   uint32_t declared_crc;
#endif

   if (!pal_engine_nor_ready ||
      image == NULL || image_size < PAL_ENGINE_PACK_HEADER_SIZE)
   {
      return false;
   }
   pack_size = read_le32(image + PAL_ENGINE_PACK_SIZE_OFFSET);
   if (pack_size == 0 || pack_size > image_size)
   {
      return false;
   }
   pack_set_id = read_le32(image + PAL_ENGINE_PACK_SET_ID_OFFSET);
   if (pack_set_id == 0u)
   {
      return false;
   }
#if defined(PAL_ENGINE_STRICT_PACK_VALIDATION)
   declared_crc = read_le32(image + PAL_ENGINE_PACK_CRC32_OFFSET);
   if (declared_crc == 0 ||
      pack_crc32_const(image, pack_size) != declared_crc)
   {
      return false;
   }
#endif
   memset(&candidate, 0, sizeof(candidate));
   if (!PalPack_OpenConst(&candidate, image, pack_size) ||
      pack_set_id != pal_engine_nor_set_id ||
      const_packs_overlap(&candidate, &pal_engine_nor_pack))
   {
      return false;
   }
   if (pal_engine_tf_ready &&
      (pack_set_id != pal_engine_tf_set_id ||
         const_pack_toc_overlap(&candidate, &pal_engine_tf_toc)))
   {
      return false;
   }

   pal_engine_overlay_pack = candidate;
   pal_engine_overlay_set_id = pack_set_id;
   pal_engine_overlay_ready = true;
   return true;
}

void
PalEngineBridge_ClearOverlay(
   void
)
{
   memset(&pal_engine_overlay_pack, 0, sizeof(pal_engine_overlay_pack));
   pal_engine_overlay_ready = false;
   pal_engine_overlay_set_id = 0;
}

void
PalEngineBridge_ClearOverlayPack(
   void
)
{
   PalEngineBridge_ClearOverlay();
}

static void
clear_tf_pack(
   void
)
{
   memset(&pal_engine_tf_toc, 0, sizeof(pal_engine_tf_toc));
   pal_engine_tf_read_at = NULL;
   pal_engine_tf_user = NULL;
   pal_engine_tf_ready = false;
   pal_engine_tf_set_id = 0;
   pal_engine_tf_map_valid = false;
}

bool
PalEngineBridge_SetTfPackReadAt(
   uint32_t pack_size,
   PalEngineBridgeReadAt read_at,
   void *user
)
{
   uint32_t tf_set_id;
#if defined(PAL_ENGINE_STRICT_PACK_VALIDATION)
   uint32_t declared_crc;
   uint32_t actual_crc;
#endif

   clear_tf_pack();
   if (read_at == NULL || pack_size < PAL_ENGINE_PACK_HEADER_SIZE)
   {
      return false;
   }

   pal_engine_tf_read_at = read_at;
   pal_engine_tf_user = user;
#if defined(PAL_ENGINE_STRICT_PACK_VALIDATION)
   if (!pal_engine_nor_ready ||
      !read_at(user, 0, PAL_ENGINE_TF_TOC_STORAGE,
         PAL_ENGINE_PACK_HEADER_SIZE))
   {
      clear_tf_pack();
      return false;
   }
   tf_set_id = read_le32(
      PAL_ENGINE_TF_TOC_STORAGE + PAL_ENGINE_PACK_SET_ID_OFFSET);
   declared_crc = read_le32(
      PAL_ENGINE_TF_TOC_STORAGE + PAL_ENGINE_PACK_CRC32_OFFSET);
   if (tf_set_id == 0 || declared_crc == 0 ||
      !pack_crc32_read_at(read_at, user, pack_size,
         PAL_ENGINE_TF_TOC_STORAGE, PAL_ENGINE_TF_TOC_BYTES, &actual_crc) ||
      actual_crc != declared_crc)
   {
      clear_tf_pack();
      return false;
   }
#endif
   if (!PalPack_OpenTocRead(&pal_engine_tf_toc,
      pal_engine_tf_read_at,
      pal_engine_tf_user,
      pack_size,
      PAL_ENGINE_TF_TOC_STORAGE,
      PAL_ENGINE_TF_TOC_BYTES))
   {
      clear_tf_pack();
      return false;
   }
   tf_set_id = read_le32(
      PAL_ENGINE_TF_TOC_STORAGE + PAL_ENGINE_PACK_SET_ID_OFFSET);
   if (tf_set_id == 0u ||
      (pal_engine_nor_ready && tf_set_id != pal_engine_nor_set_id) ||
      (pal_engine_overlay_ready &&
         tf_set_id != pal_engine_overlay_set_id) ||
#if !defined(PAL_STORAGE_SD_ONLY)
      (pal_engine_nor_ready &&
         const_pack_toc_overlap(&pal_engine_nor_pack,
            &pal_engine_tf_toc)) ||
#endif
      (pal_engine_overlay_ready &&
         const_pack_toc_overlap(&pal_engine_overlay_pack,
            &pal_engine_tf_toc)))
   {
      clear_tf_pack();
      return false;
   }

   pal_engine_tf_set_id = tf_set_id;
   pal_engine_tf_ready = true;
   return true;
}

bool
PalEngineBridge_GetActivePackSetId(
   uint32_t *pack_set_id
)
{
   ensure_default_packs();
   if (pack_set_id == NULL || !pal_engine_nor_ready ||
      !pal_engine_tf_ready || pal_engine_nor_set_id == 0u ||
      pal_engine_nor_set_id != pal_engine_tf_set_id ||
      (pal_engine_overlay_ready &&
         pal_engine_overlay_set_id != pal_engine_nor_set_id))
   {
      return false;
   }
   *pack_set_id = pal_engine_nor_set_id;
   return true;
}

bool
PalEngineBridge_HasNativeChunk(
   uint16_t archive_id,
   uint16_t chunk_id
)
{
   PalPackSpan span;
   PalPackChunkInfo info;
   PalEngineArchiveStore store;

   ensure_default_packs();
   store = find_chunk_store(archive_id, chunk_id, &span, &info);
   if (store == PAL_ENGINE_ARCHIVE_STORE_OVERLAY ||
      store == PAL_ENGINE_ARCHIVE_STORE_CORE)
   {
      return span.data != NULL && span.size != 0u &&
         span.format == PAL_PACK_FORMAT_NATIVE &&
         (span.flags & PAL_PACK_CHUNK_F_COMPRESSED) == 0u;
   }
   return store == PAL_ENGINE_ARCHIVE_STORE_TF &&
      info.size != 0u && info.format == PAL_PACK_FORMAT_NATIVE &&
      (info.flags & PAL_PACK_CHUNK_F_COMPRESSED) == 0u;
}

void
PalEngineBridge_ClearPacks(
   void
)
{
   memset(&pal_engine_nor_pack, 0, sizeof(pal_engine_nor_pack));
   PalEngineBridge_ClearOverlay();
   clear_tf_pack();
   pal_engine_nor_ready = false;
   pal_engine_nor_set_id = 0;
   pal_engine_default_tried = false;
}

FILE *
__wrap_PAL_MKFOpenPackArchive(
   UINT archive_id
)
{
   uint16_t count;
   PalEngineArchiveStore store;

   ensure_default_packs();
   store = find_archive_store(archive_id, &count);
   if (store == PAL_ENGINE_ARCHIVE_STORE_NONE)
   {
      return NULL;
   }
   (void)count;

   return (FILE *)(uintptr_t)(PAL_ENGINE_PACK_FILE_TAG | (uintptr_t)archive_id);
}

BOOL
__wrap_PAL_MKFIsPackArchive(
   FILE *fp
)
{
   return archive_from_file(fp) != 0;
}

bool
PalEngineBridge_IsPackFile(
   FILE *fp
)
{
   return archive_from_file(fp) != 0;
}

int
__wrap_fclose(
   FILE *stream
) __attribute__((weak));

int
__wrap_fclose(
   FILE *stream
)
{
   if (PalEngineBridge_IsPackFile(stream))
   {
      return 0;
   }
   return __real_fclose(stream);
}

INT
__wrap_PAL_MKFGetChunkCount(
   FILE *fp
)
{
   UINT archive_id;
   uint16_t count;
   PalEngineArchiveStore store;

   ensure_default_packs();
   archive_id = archive_from_file(fp);
   store = find_archive_store(archive_id, &count);
   return store == PAL_ENGINE_ARCHIVE_STORE_NONE ? 0 : (INT)count;
}

INT
__wrap_PAL_MKFGetChunkSize(
   UINT chunk_id,
   FILE *fp
)
{
   UINT archive_id;
   PalPackSpan span;
   PalPackChunkInfo info;
   PalEngineArchiveStore store;

   ensure_default_packs();
   archive_id = archive_from_file(fp);
   if (archive_id == 0 || chunk_id > 0xffffu)
   {
      return -1;
   }
   store = find_chunk_store((uint16_t)archive_id, (uint16_t)chunk_id,
      &span, &info);
   return (store == PAL_ENGINE_ARCHIVE_STORE_OVERLAY ||
      store == PAL_ENGINE_ARCHIVE_STORE_CORE) ? (INT)span.size :
      (store == PAL_ENGINE_ARCHIVE_STORE_TF ? (INT)info.size : -1);
}

BOOL
__wrap_PAL_MKFMapChunk(
   FILE *fp,
   UINT chunk_id,
   LPCBYTE *data,
   UINT *size
)
{
   UINT archive_id;
   PalPackSpan span;
   PalPackChunkInfo info;
   PalEngineArchiveStore store;

   ensure_default_packs();
   if (data == NULL || size == NULL || chunk_id > 0xffffu)
   {
      return FALSE;
   }

   archive_id = archive_from_file(fp);
   if (archive_id == 0)
   {
      return FALSE;
   }
   store = find_chunk_store((uint16_t)archive_id, (uint16_t)chunk_id,
      &span, &info);
   if (store == PAL_ENGINE_ARCHIVE_STORE_OVERLAY ||
      store == PAL_ENGINE_ARCHIVE_STORE_CORE)
   {
      *data = span.data;
      *size = span.size;
      return TRUE;
   }
   if (store != PAL_ENGINE_ARCHIVE_STORE_TF)
   {
      return FALSE;
   }

#if defined(PAL_STORAGE_SD_ONLY)
   if (!PalPackToc_GetChunkInfo(&pal_engine_tf_toc,
         (uint16_t)archive_id, (uint16_t)chunk_id, &info) ||
      info.size > PAL_ENGINE_TF_MAP_BYTES)
   {
      return FALSE;
   }
   if (!pal_engine_tf_map_valid ||
      pal_engine_tf_map_archive != (uint16_t)archive_id ||
      pal_engine_tf_map_chunk != (uint16_t)chunk_id ||
      pal_engine_tf_map_size != info.size)
   {
      if (info.size != 0u &&
         !pal_engine_tf_read_at(pal_engine_tf_user, info.offset,
            PAL_ENGINE_TF_MAP_STORAGE, info.size))
      {
         pal_engine_tf_map_valid = false;
         return FALSE;
      }
      pal_engine_tf_map_archive = (uint16_t)archive_id;
      pal_engine_tf_map_chunk = (uint16_t)chunk_id;
      pal_engine_tf_map_size = info.size;
      pal_engine_tf_map_valid = true;
   }
   *data = PAL_ENGINE_TF_MAP_STORAGE;
   *size = info.size;
   return TRUE;
#elif defined(MEM_LEVEL1)
   /*
    * TF is deliberately read-only/streaming in the no-PSRAM profile. Every
    * asset used as a const per-frame view must be selected into the NOR pack.
    */
   return FALSE;
#else
   if (!PalPackToc_GetChunkInfo(&pal_engine_tf_toc, (uint16_t)archive_id, (uint16_t)chunk_id, &info) ||
      info.size > PAL_ENGINE_TF_MAP_BYTES)
   {
      return FALSE;
   }
   if (!pal_engine_tf_map_valid ||
      pal_engine_tf_map_archive != (uint16_t)archive_id ||
      pal_engine_tf_map_chunk != (uint16_t)chunk_id ||
      pal_engine_tf_map_size != info.size)
   {
      if (info.size != 0 &&
         !pal_engine_tf_read_at(pal_engine_tf_user, info.offset, PAL_ENGINE_TF_MAP_STORAGE, info.size))
      {
         pal_engine_tf_map_valid = false;
         return FALSE;
      }
      pal_engine_tf_map_archive = (uint16_t)archive_id;
      pal_engine_tf_map_chunk = (uint16_t)chunk_id;
      pal_engine_tf_map_size = info.size;
      pal_engine_tf_map_valid = true;
   }

   *data = PAL_ENGINE_TF_MAP_STORAGE;
   *size = info.size;
   return TRUE;
#endif
}

INT
__wrap_PAL_MKFReadChunk(
   LPBYTE buffer,
   UINT buffer_size,
   UINT chunk_id,
   FILE *fp
)
{
   UINT archive_id;
   PalPackSpan span;
   PalPackChunkInfo info;
   PalEngineArchiveStore store;

   ensure_default_packs();
   if (buffer == NULL || fp == NULL || buffer_size == 0 || chunk_id > 0xffffu)
   {
      return -1;
   }

   archive_id = archive_from_file(fp);
   if (archive_id == 0)
   {
      return -1;
   }
   store = find_chunk_store((uint16_t)archive_id, (uint16_t)chunk_id,
      &span, &info);
   if (store == PAL_ENGINE_ARCHIVE_STORE_TF)
   {
      if (info.size > buffer_size)
      {
         return -2;
      }
      if (info.size == 0)
      {
         return -1;
      }
      return pal_engine_tf_read_at(pal_engine_tf_user, info.offset, buffer, info.size)
         ? (INT)info.size : -1;
   }
   if (store != PAL_ENGINE_ARCHIVE_STORE_OVERLAY &&
      store != PAL_ENGINE_ARCHIVE_STORE_CORE)
   {
      return -1;
   }

   if (span.size > buffer_size)
   {
      return -2;
   }
   if (span.size == 0)
   {
      return -1;
   }
   memcpy(buffer, span.data, span.size);
   return (INT)span.size;
}

int
PalEngineBridge_GetNativeChunkSize(
   FILE        *fp,
   uint16_t     chunk_id
)
{
   UINT archive_id;
   PalPackSpan span;
   PalPackChunkInfo info;
   PalEngineArchiveStore store;

   ensure_default_packs();
   if (fp == NULL || (archive_id = archive_from_file(fp)) == 0)
   {
      return -1;
   }
   store = find_chunk_store((uint16_t)archive_id, chunk_id, &span, &info);
   if (store == PAL_ENGINE_ARCHIVE_STORE_OVERLAY ||
      store == PAL_ENGINE_ARCHIVE_STORE_CORE)
   {
      return span.size <= INT_MAX ? (int)span.size : -1;
   }
   if (store == PAL_ENGINE_ARCHIVE_STORE_TF)
   {
      return info.size <= INT_MAX ? (int)info.size : -1;
   }
   return -1;
}

bool
PalEngineBridge_ReadNativeChunkRange(
   FILE        *fp,
   uint16_t     chunk_id,
   uint32_t     chunk_offset,
   uint8_t     *dst,
   uint32_t     size
)
{
   UINT archive_id;
   PalPackSpan span;
   PalPackChunkInfo info;
   PalEngineArchiveStore store;
   uint32_t chunk_size;

   ensure_default_packs();
   if ((dst == NULL && size != 0u) || fp == NULL)
   {
      return false;
   }
   archive_id = archive_from_file(fp);
   if (archive_id == 0)
   {
      return false;
   }
   store = find_chunk_store((uint16_t)archive_id, chunk_id, &span, &info);
   if (store == PAL_ENGINE_ARCHIVE_STORE_OVERLAY ||
      store == PAL_ENGINE_ARCHIVE_STORE_CORE)
   {
      chunk_size = span.size;
   }
   else if (store == PAL_ENGINE_ARCHIVE_STORE_TF)
   {
      chunk_size = info.size;
   }
   else
   {
      return false;
   }
   if (chunk_offset > chunk_size || size > chunk_size - chunk_offset)
   {
      return false;
   }
   if (size == 0u)
   {
      return true;
   }
   if (store == PAL_ENGINE_ARCHIVE_STORE_OVERLAY ||
      store == PAL_ENGINE_ARCHIVE_STORE_CORE)
   {
      memcpy(dst, span.data + chunk_offset, size);
      return true;
   }
   if (info.offset > UINT32_MAX - chunk_offset)
   {
      return false;
   }
   return pal_engine_tf_read_at != NULL &&
      pal_engine_tf_read_at(pal_engine_tf_user,
         info.offset + chunk_offset, dst, size);
}

bool
PalContract_TargetOpenNorPack(
   PalPack *pack
)
{
   ensure_default_packs();
   if (pack == NULL || !pal_engine_nor_ready)
   {
      return false;
   }
   *pack = pal_engine_nor_pack;
   return true;
}

bool
PalContract_TargetOpenTfPack(
   PalPack *pack
)
{
   (void)pack;
   ensure_default_packs();
   return false;
}

#if defined(PAL_EXTREME_TWO_SCREENS)
bool
PalEngineBridge_OpenNativeRngFrame(
   uint16_t movie_id,
   uint16_t frame_id,
   PalEngineBridgeNativeRngFrame *frame
)
{
   PalPackSpan span;
   PalPackChunkInfo info;
   PalEngineArchiveStore store;
   uint8_t words[8];
   uint32_t frame_count;
   uint32_t table_bytes;
   uint32_t table_offset;
   uint32_t start;
   uint32_t end;

   ensure_default_packs();
   if (frame == NULL)
   {
      return false;
   }
   memset(frame, 0, sizeof(*frame));

   store = find_chunk_store(PAL_PACK_ARCHIVE_RNG, movie_id,
      &span, &info);
   if ((store == PAL_ENGINE_ARCHIVE_STORE_OVERLAY ||
      store == PAL_ENGINE_ARCHIVE_STORE_CORE) &&
      span.size >= 4)
   {
      frame_count = read_le32(span.data);
      if (frame_id >= frame_count ||
         frame_count > (UINT32_MAX - 8u) / 4u ||
         span.size < 4u + (frame_count + 1u) * 4u)
      {
         return false;
      }
      table_bytes = 4u + (frame_count + 1u) * 4u;
      start = read_le32(span.data + 4u + (uint32_t)frame_id * 4u);
      end = read_le32(span.data + 8u + (uint32_t)frame_id * 4u);
      if (start < table_bytes || start >= end || end > span.size)
      {
         return false;
      }
      frame->mapped_data = span.data + start;
      frame->size = end - start;
      return true;
   }

   if (store != PAL_ENGINE_ARCHIVE_STORE_TF || info.size < 4 ||
      pal_engine_tf_read_at == NULL ||
      !pal_engine_tf_read_at(pal_engine_tf_user, info.offset, words, 4))
   {
      return false;
   }
   frame_count = read_le32(words);
   if (frame_id >= frame_count ||
      frame_count > (UINT32_MAX - 8u) / 4u ||
      info.size < 4u + (frame_count + 1u) * 4u)
   {
      return false;
   }
   table_bytes = 4u + (frame_count + 1u) * 4u;
   table_offset = 4u + (uint32_t)frame_id * 4u;
   if (info.offset > UINT32_MAX - table_offset ||
      !pal_engine_tf_read_at(pal_engine_tf_user,
         info.offset + table_offset,
         words,
         sizeof(words)))
   {
      return false;
   }
   start = read_le32(words);
   end = read_le32(words + 4);
   if (start < table_bytes || start >= end || end > info.size ||
      info.offset > UINT32_MAX - start)
   {
      return false;
   }
   frame->tf_offset = info.offset + start;
   frame->size = end - start;
   frame->tf_backed = true;
   return true;
}

bool
PalEngineBridge_ReadNativeRngFrameRange(
   const PalEngineBridgeNativeRngFrame *frame,
   uint32_t frame_offset,
   uint8_t *dst,
   uint32_t size
)
{
   if (frame == NULL || (dst == NULL && size != 0u) ||
      frame_offset > frame->size || size > frame->size - frame_offset)
   {
      return false;
   }
   if (size == 0u)
   {
      return true;
   }
   if (frame->mapped_data != NULL)
   {
      memcpy(dst, frame->mapped_data + frame_offset, size);
      return true;
   }
   if (!frame->tf_backed || frame->tf_offset > UINT32_MAX - frame_offset ||
      pal_engine_tf_read_at == NULL)
   {
      return false;
   }
   return pal_engine_tf_read_at(pal_engine_tf_user,
      frame->tf_offset + frame_offset, dst, size);
}

int
PalEngineBridge_ReadNativeRngFrame(
   uint16_t movie_id,
   uint16_t frame_id,
   uint8_t *dst,
   uint32_t dst_capacity
)
{
   PalEngineBridgeNativeRngFrame frame;

   if (dst == NULL || dst_capacity == 0u ||
      !PalEngineBridge_OpenNativeRngFrame(movie_id, frame_id, &frame))
   {
      return -1;
   }
   if (frame.size > dst_capacity)
   {
      return -2;
   }
   if (frame.size > INT_MAX ||
      !PalEngineBridge_ReadNativeRngFrameRange(&frame, 0u, dst, frame.size))
   {
      return -1;
   }
   return (int)frame.size;
}
#endif
