#include "pal_engine_pack_provider.h"

#include "palcommon.h"
#include "pal_pack.h"

#include <stddef.h>
#include <stdint.h>
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
#define PAL_ENGINE_PACK_SIZE_OFFSET 24u
#define PAL_ENGINE_TF_TOC_BYTES (32u * 1024u)
#define PAL_ENGINE_TF_MAP_BYTES (2176u * 1024u)

#if defined(__GNUC__)
#if defined(EXT_RAM_BSS_ATTR)
#define PAL_ENGINE_PSRAM EXT_RAM_BSS_ATTR __attribute__((aligned(4)))
#else
#define PAL_ENGINE_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#endif
#else
#define PAL_ENGINE_PSRAM
#endif

typedef enum PalEngineArchiveStore {
   PAL_ENGINE_ARCHIVE_STORE_NONE = 0,
   PAL_ENGINE_ARCHIVE_STORE_NOR,
   PAL_ENGINE_ARCHIVE_STORE_TF
} PalEngineArchiveStore;

static PalPack pal_engine_nor_pack;
static PalPackToc pal_engine_tf_toc;
static PalEngineBridgeReadAt pal_engine_tf_read_at;
static void *pal_engine_tf_user;
static bool pal_engine_nor_ready;
static bool pal_engine_tf_ready;
static bool pal_engine_default_tried;
static bool pal_engine_tf_map_valid;
static uint16_t pal_engine_tf_map_archive;
static uint16_t pal_engine_tf_map_chunk;
static uint32_t pal_engine_tf_map_size;
static uint8_t pal_psram_engine_tf_toc[PAL_ENGINE_TF_TOC_BYTES] PAL_ENGINE_PSRAM;
static uint8_t pal_psram_engine_tf_map[PAL_ENGINE_TF_MAP_BYTES] PAL_ENGINE_PSRAM;

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
   uint16_t nor_count = 0;
   uint16_t tf_count = 0;
   bool in_nor;
   bool in_tf;

   if (archive_id == 0 || archive_id > 0xffffu)
   {
      return PAL_ENGINE_ARCHIVE_STORE_NONE;
   }

   in_nor = pal_engine_nor_ready &&
      PalPack_GetChunkCount(&pal_engine_nor_pack, (uint16_t)archive_id, &nor_count);
   in_tf = pal_engine_tf_ready &&
      PalPackToc_GetChunkCount(&pal_engine_tf_toc, (uint16_t)archive_id, &tf_count);

   if (in_nor == in_tf)
   {
      return PAL_ENGINE_ARCHIVE_STORE_NONE;
   }
   if (chunk_count != NULL)
   {
      *chunk_count = in_tf ? tf_count : nor_count;
   }
   return in_tf ? PAL_ENGINE_ARCHIVE_STORE_TF : PAL_ENGINE_ARCHIVE_STORE_NOR;
}

bool
PalEngineBridge_SetNorPackConst(
   const uint8_t *image,
   uint32_t image_size
)
{
   uint32_t pack_size;

   pal_engine_nor_ready = false;
   memset(&pal_engine_nor_pack, 0, sizeof(pal_engine_nor_pack));
   if (image == NULL || image_size < PAL_ENGINE_PACK_HEADER_SIZE)
   {
      return false;
   }

   pack_size = read_le32(image + PAL_ENGINE_PACK_SIZE_OFFSET);
   if (pack_size == 0 || pack_size > image_size)
   {
      return false;
   }
   pal_engine_nor_ready = PalPack_OpenConst(&pal_engine_nor_pack, image, pack_size);
   return pal_engine_nor_ready;
}

bool
PalEngineBridge_SetTfPackReadAt(
   uint32_t pack_size,
   PalEngineBridgeReadAt read_at,
   void *user
)
{
   pal_engine_tf_ready = false;
   pal_engine_tf_read_at = NULL;
   pal_engine_tf_user = NULL;
   pal_engine_tf_map_valid = false;
   memset(&pal_engine_tf_toc, 0, sizeof(pal_engine_tf_toc));
   if (read_at == NULL || pack_size < PAL_ENGINE_PACK_HEADER_SIZE)
   {
      return false;
   }

   pal_engine_tf_read_at = read_at;
   pal_engine_tf_user = user;
   pal_engine_tf_ready = PalPack_OpenTocRead(&pal_engine_tf_toc,
      pal_engine_tf_read_at,
      pal_engine_tf_user,
      pack_size,
      pal_psram_engine_tf_toc,
      PAL_ENGINE_TF_TOC_BYTES);
   if (!pal_engine_tf_ready)
   {
      pal_engine_tf_read_at = NULL;
      pal_engine_tf_user = NULL;
   }
   return pal_engine_tf_ready;
}

void
PalEngineBridge_ClearPacks(
   void
)
{
   memset(&pal_engine_nor_pack, 0, sizeof(pal_engine_nor_pack));
   memset(&pal_engine_tf_toc, 0, sizeof(pal_engine_tf_toc));
   pal_engine_tf_read_at = NULL;
   pal_engine_tf_user = NULL;
   pal_engine_nor_ready = false;
   pal_engine_tf_ready = false;
   pal_engine_default_tried = false;
   pal_engine_tf_map_valid = false;
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
   store = find_archive_store(archive_id, NULL);
   if (store == PAL_ENGINE_ARCHIVE_STORE_TF)
   {
      if (!PalPackToc_GetChunkInfo(&pal_engine_tf_toc, (uint16_t)archive_id, (uint16_t)chunk_id, &info))
      {
         return -1;
      }
      return (INT)info.size;
   }
   if (store != PAL_ENGINE_ARCHIVE_STORE_NOR)
   {
      return -1;
   }

   if (!PalPack_MapConst(&pal_engine_nor_pack, (uint16_t)archive_id, (uint16_t)chunk_id, &span))
   {
      return -1;
   }
   return (INT)span.size;
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
   store = find_archive_store(archive_id, NULL);
   if (store == PAL_ENGINE_ARCHIVE_STORE_NOR)
   {
      if (!PalPack_MapConst(&pal_engine_nor_pack, (uint16_t)archive_id, (uint16_t)chunk_id, &span))
      {
         return FALSE;
      }
      *data = span.data;
      *size = span.size;
      return TRUE;
   }
   if (store != PAL_ENGINE_ARCHIVE_STORE_TF)
   {
      return FALSE;
   }

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
         !pal_engine_tf_read_at(pal_engine_tf_user, info.offset, pal_psram_engine_tf_map, info.size))
      {
         pal_engine_tf_map_valid = false;
         return FALSE;
      }
      pal_engine_tf_map_archive = (uint16_t)archive_id;
      pal_engine_tf_map_chunk = (uint16_t)chunk_id;
      pal_engine_tf_map_size = info.size;
      pal_engine_tf_map_valid = true;
   }

   *data = pal_psram_engine_tf_map;
   *size = info.size;
   return TRUE;
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
   store = find_archive_store(archive_id, NULL);
   if (store == PAL_ENGINE_ARCHIVE_STORE_TF)
   {
      if (!PalPackToc_GetChunkInfo(&pal_engine_tf_toc, (uint16_t)archive_id, (uint16_t)chunk_id, &info))
      {
         return -1;
      }
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
   if (store != PAL_ENGINE_ARCHIVE_STORE_NOR)
   {
      return -1;
   }

   if (!PalPack_MapConst(&pal_engine_nor_pack, (uint16_t)archive_id, (uint16_t)chunk_id, &span))
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
