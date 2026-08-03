#include "pal_engine_pack_provider.h"

#include "pal_target_board.h"
#include "pal_memory_profile.h"
#if defined(PAL_EXTREME_TWO_SCREENS) || defined(PAL_EXTREME_CHAPTER_CACHE)
#include "../../embedded/pal_font10_cache.h"
#include "../../embedded/pal_native_ui.h"
#endif
#if defined(PAL_EXTREME_CHAPTER_CACHE)
#include "pal_engine_chapter_cache.h"
#endif

#include <esp_err.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <ff.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#define PAL_ENGINE_NOR_PARTITION_SUBTYPE 0x40
#define PAL_ENGINE_PACK_HEADER_BYTES 32u
#define PAL_ENGINE_PACK_SIZE_OFFSET 24u
#if defined(PAL_EXTREME_CHAPTER_CACHE)
#define PAL_ENGINE_NOR_PARTITION_LABEL "pal_core"
#define PAL_ENGINE_CACHE_CATALOG_ARCHIVE 20u
#define PAL_ENGINE_PACK_SET_ID_OFFSET 20u
#else
#define PAL_ENGINE_NOR_PARTITION_LABEL "pal_nor"
#endif
#if defined(PAL_EXTREME_CHAPTER_CACHE) || defined(PAL_STORAGE_SD_ONLY)
#define PAL_ENGINE_TF_PACK_PATH "0:/pal_full.pak"
#else
#define PAL_ENGINE_TF_PACK_PATH "0:/pal_tf.pak"
#endif

static const char *TAG = "pal_engine_packs";
static FIL pal_engine_tf_file;
static bool pal_engine_tf_open;
#if defined(PAL_STORAGE_SD_ONLY)
static const uint16_t pal_level2_resident_archives[] = {
   PAL_PACK_ARCHIVE_DATA,
   PAL_PACK_ARCHIVE_MUS,
   PAL_PACK_ARCHIVE_PAT,
   PAL_PACK_ARCHIVE_RGM,
   PAL_PACK_ARCHIVE_SSS,
   PAL_PACK_ARCHIVE_TEXT,
   PAL_PACK_ARCHIVE_FONT,
};
#else
static esp_partition_mmap_handle_t pal_engine_nor_mmap_handle;
static uint8_t pal_sram_engine_pack_header[PAL_ENGINE_PACK_HEADER_BYTES];
#endif

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

#if defined(PAL_STORAGE_SD_ONLY)
static void
write_le16(
   uint8_t *p,
   uint16_t value
)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
}

static void
write_le32(
   uint8_t *p,
   uint32_t value
)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
   p[2] = (uint8_t)(value >> 16);
   p[3] = (uint8_t)(value >> 24);
}

static uint32_t
align4(
   uint32_t value
)
{
   return (value + 3u) & ~3u;
}
#endif

#if defined(PAL_EXTREME_CHAPTER_CACHE)
static bool
chapter_overlay_changed(
   void *user,
   const uint8_t *image,
   uint32_t image_size
)
{
   (void)user;
   if (image == NULL)
   {
      PalEngineBridge_ClearOverlayPack();
      return true;
   }
   return PalEngineBridge_SetOverlayPackConst(image, image_size);
}
#endif

static bool
tf_read_at(
   void *user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size
)
{
   FIL *file = (FIL *)user;
   UINT got = 0;

   if (file == NULL || (dst == NULL && size != 0u))
   {
      return false;
   }
   PalTarget_PrepareTfAccess();
   if (f_lseek(file, (FSIZE_t)offset) != FR_OK)
   {
      return false;
   }
   return f_read(file, dst, (UINT)size, &got) == FR_OK && got == (UINT)size;
}

#if defined(PAL_STORAGE_SD_ONLY)
static bool
sd_only_copy_full_range(
   uint32_t offset,
   uint8_t *dst,
   uint32_t size
)
{
   while (size != 0u)
   {
      uint32_t amount = size > 32768u ? 32768u : size;

      if (!tf_read_at(&pal_engine_tf_file, offset, dst, amount))
      {
         return false;
      }
      offset += amount;
      dst += amount;
      size -= amount;
   }
   return true;
}

static bool
sd_only_resident_chunk_selected(
   uint16_t archive_id,
   uint16_t chunk_id
)
{
   /* The native 10px FONT chunk supersedes the legacy 16px DOS font. */
   return archive_id != PAL_PACK_ARCHIVE_FONT || chunk_id == 1u;
}

static bool
sd_only_build_resident_pack(
   const PalPackToc *full_toc,
   uint32_t *out_size
)
{
   enum {
      PACK_HEADER_BYTES = 32u,
      ARCHIVE_ENTRY_BYTES = 12u,
      CHUNK_ENTRY_BYTES = 16u,
   };
   uint8_t *image = pal_mem_level2_resident_pack;
   const uint16_t archive_count = (uint16_t)(
      sizeof(pal_level2_resident_archives) /
      sizeof(pal_level2_resident_archives[0]));
   uint16_t chunk_counts[
      sizeof(pal_level2_resident_archives) /
      sizeof(pal_level2_resident_archives[0])];
   uint32_t archive_table = PACK_HEADER_BYTES;
   uint32_t chunk_table = archive_table +
      (uint32_t)archive_count * ARCHIVE_ENTRY_BYTES;
   uint32_t data_offset;
   uint32_t cursor;
   uint16_t archive_index;

   if (full_toc == NULL || full_toc->base == NULL || out_size == NULL)
   {
      return false;
   }

   for (archive_index = 0u; archive_index < archive_count; archive_index++)
   {
      if (!PalPackToc_GetChunkCount(full_toc,
            pal_level2_resident_archives[archive_index],
            &chunk_counts[archive_index]) ||
         chunk_counts[archive_index] >
            (PAL_MEM_LEVEL2_RESIDENT_PACK_BYTES - chunk_table) /
            CHUNK_ENTRY_BYTES)
      {
         return false;
      }
      chunk_table +=
         (uint32_t)chunk_counts[archive_index] * CHUNK_ENTRY_BYTES;
   }
   data_offset = align4(chunk_table);
   if (data_offset > PAL_MEM_LEVEL2_RESIDENT_PACK_BYTES)
   {
      return false;
   }
   memset(image, 0, data_offset);
   chunk_table = archive_table +
      (uint32_t)archive_count * ARCHIVE_ENTRY_BYTES;
   cursor = data_offset;

   for (archive_index = 0u; archive_index < archive_count; archive_index++)
   {
      uint16_t archive_id = pal_level2_resident_archives[archive_index];
      uint16_t chunk_count = chunk_counts[archive_index];
      uint8_t *archive_entry = image + archive_table +
         (uint32_t)archive_index * ARCHIVE_ENTRY_BYTES;
      uint32_t source_first = 0u;
      uint32_t source_end = 0u;
      uint32_t destination_first;
      uint16_t chunk_id;

      write_le16(archive_entry, archive_id);
      write_le16(archive_entry + 2u, chunk_count);
      write_le32(archive_entry + 4u, chunk_table);
      for (chunk_id = 0u; chunk_id < chunk_count; chunk_id++)
      {
         PalPackChunkInfo info;

         if (!PalPackToc_GetChunkInfo(
               full_toc, archive_id, chunk_id, &info) ||
            info.flags != 0u)
         {
            return false;
         }
         if (info.size != 0u &&
            sd_only_resident_chunk_selected(archive_id, chunk_id))
         {
            if (source_first == 0u)
            {
               source_first = info.offset;
            }
            else if (info.offset != align4(source_end))
            {
               return false;
            }
            if (info.offset > UINT32_MAX - info.size)
            {
               return false;
            }
            source_end = info.offset + info.size;
         }
      }
      destination_first = align4(cursor);
      if (source_first != 0u)
      {
         uint32_t span = source_end - source_first;

         if (destination_first > PAL_MEM_LEVEL2_RESIDENT_PACK_BYTES ||
            span > PAL_MEM_LEVEL2_RESIDENT_PACK_BYTES - destination_first ||
            !sd_only_copy_full_range(
               source_first, image + destination_first, span))
         {
            return false;
         }
         cursor = destination_first + span;
      }
      for (chunk_id = 0u; chunk_id < chunk_count; chunk_id++)
      {
         PalPackChunkInfo info;
         uint8_t *chunk_entry = image + chunk_table +
            (uint32_t)chunk_id * CHUNK_ENTRY_BYTES;
         uint32_t destination = destination_first;

         if (!PalPackToc_GetChunkInfo(
               full_toc, archive_id, chunk_id, &info))
         {
            return false;
         }
         if (!sd_only_resident_chunk_selected(archive_id, chunk_id))
         {
            info.size = 0u;
         }
         if (info.size != 0u)
         {
            destination += info.offset - source_first;
         }
         write_le32(chunk_entry, destination);
         write_le32(chunk_entry + 4u, info.size);
         write_le16(chunk_entry + 8u, info.format);
         write_le16(chunk_entry + 10u, info.flags);
      }
      chunk_table += (uint32_t)chunk_count * CHUNK_ENTRY_BYTES;
   }

   write_le32(image, PAL_PACK_MAGIC);
   write_le16(image + 4u, PAL_PACK_VERSION);
   write_le16(image + 6u, PACK_HEADER_BYTES);
   write_le16(image + 8u, archive_count);
   write_le32(image + 12u, archive_table);
   write_le32(image + 16u, data_offset);
   write_le32(image + 20u, read_le32(full_toc->base + 20u));
   write_le32(image + 24u, cursor);
   *out_size = cursor;
   return true;
}

static bool
sd_only_init_packs(
   void
)
{
   PalPack core_pack;
   PalPackToc full_toc;
   PalFont10Cache font10;
   uint32_t core_size;
   uint32_t full_size;
   FRESULT result;

   PalEngineBridge_ClearPacks();
   if (!PalTarget_MountTf())
   {
      return false;
   }
   PalTarget_PrepareTfAccess();
   result = f_open(&pal_engine_tf_file,
      PAL_ENGINE_TF_PACK_PATH, FA_READ | FA_OPEN_EXISTING);
   if (result != FR_OK)
   {
      ESP_LOGE(TAG, "SD gameplay pack open failed: %s (%d)",
         PAL_ENGINE_TF_PACK_PATH, (int)result);
      return false;
   }
   pal_engine_tf_open = true;
   full_size = (uint32_t)f_size(&pal_engine_tf_file);
   if (!PalPack_OpenTocRead(&full_toc, tf_read_at, &pal_engine_tf_file,
         full_size, pal_mem_level2_tf_toc,
         PAL_MEM_LEVEL2_TF_TOC_BYTES) ||
      !sd_only_build_resident_pack(&full_toc, &core_size) ||
      !PalPack_OpenConst(
         &core_pack, pal_mem_level2_resident_pack, core_size) ||
      !PalFont10_Open(&core_pack, &font10) ||
      font10.cell_width != 10u || font10.cell_height != 10u ||
      !PalEngineBridge_SetCorePackConst(
         pal_mem_level2_resident_pack, core_size))
   {
      ESP_LOGE(TAG,
         "SD resident view construction or FONT10 validation failed");
      return false;
   }

   if (!PalEngineBridge_SetTfPackReadAt(
         full_size,
         tf_read_at,
         &pal_engine_tf_file))
   {
      ESP_LOGE(TAG, "SD gameplay pack validation failed: %s",
         PAL_ENGINE_TF_PACK_PATH);
      return false;
   }
   ESP_LOGI(TAG,
      "SD-only pack ready: resident=%" PRIu32 " full=%" PRIu32,
      core_size, full_size);
   return true;
}
#endif

bool
PalEngineBridge_TargetInitPacks(
   void
)
{
#if defined(PAL_STORAGE_SD_ONLY)
   return sd_only_init_packs();
#else
   const esp_partition_t *partition;
   const void *nor_image = NULL;
   esp_err_t err;
   uint32_t nor_pack_size;
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   PalPack core_pack;
   PalPackSpan core_catalog_span;
   PalFont10Cache font10;
   uint32_t core_set_id;
   const uint8_t *catalog_image = NULL;
   uint32_t catalog_size = 0u;
#elif defined(PAL_EXTREME_TWO_SCREENS)
   PalPack nor_pack;
   PalFont10Cache font10;
#endif

   PalEngineBridge_ClearPacks();
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   if (!PalTarget_MountTf())
   {
      ESP_LOGE(TAG, "TF mount failed before core verification");
      return false;
   }
   if (!PalEngineChapterCache_TargetPrepareCore(
         &catalog_image, &catalog_size, &core_set_id))
   {
      ESP_LOGE(TAG, "TF core preparation failed");
      return false;
   }
#endif
   partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      PAL_ENGINE_NOR_PARTITION_SUBTYPE,
      PAL_ENGINE_NOR_PARTITION_LABEL);
   if (partition == NULL)
   {
      ESP_LOGE(TAG, "missing NOR partition: %s", PAL_ENGINE_NOR_PARTITION_LABEL);
      return false;
   }

   if (partition->size < sizeof(pal_sram_engine_pack_header) ||
      esp_partition_read(partition, 0u, pal_sram_engine_pack_header,
         sizeof(pal_sram_engine_pack_header)) != ESP_OK)
   {
      ESP_LOGE(TAG, "NOR pack header read failed");
      return false;
   }
   nor_pack_size = read_le32(
      pal_sram_engine_pack_header + PAL_ENGINE_PACK_SIZE_OFFSET);
   if (nor_pack_size < sizeof(pal_sram_engine_pack_header) ||
      nor_pack_size > partition->size)
   {
      ESP_LOGE(TAG, "invalid NOR pack size");
      return false;
   }
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   if (read_le32(pal_sram_engine_pack_header +
         PAL_ENGINE_PACK_SET_ID_OFFSET) != core_set_id)
   {
      ESP_LOGE(TAG, "core pack and TF set IDs differ");
      return false;
   }
#endif
   err = esp_partition_mmap(partition,
      0, nor_pack_size, ESP_PARTITION_MMAP_DATA,
      &nor_image, &pal_engine_nor_mmap_handle);
   if (err != ESP_OK)
   {
      ESP_LOGE(TAG, "NOR pack open failed: %s", esp_err_to_name(err));
      return false;
   }
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   if (!PalPack_OpenConst(&core_pack,
         (const uint8_t *)nor_image, nor_pack_size) ||
      !PalFont10_Open(&core_pack, &font10) ||
      font10.cell_width != 10u || font10.cell_height != 10u ||
      !PalPack_MapConst(&core_pack,
         PAL_ENGINE_CACHE_CATALOG_ARCHIVE, 0u, &core_catalog_span) ||
      core_catalog_span.size != catalog_size ||
      memcmp(core_catalog_span.data, catalog_image, catalog_size) != 0 ||
      !PalEngineBridge_SetCorePackConst(
         (const uint8_t *)nor_image, nor_pack_size))
   {
      esp_partition_munmap(pal_engine_nor_mmap_handle);
      pal_engine_nor_mmap_handle = 0;
      ESP_LOGE(TAG,
         "core pack, FONT10 geometry, or external catalog validation failed");
      return false;
   }
#else
   if (
#if defined(PAL_EXTREME_TWO_SCREENS)
      !PalPack_OpenConst(
         &nor_pack, (const uint8_t *)nor_image, nor_pack_size) ||
      !PalFont10_Open(&nor_pack, &font10) ||
      !PalNativeUi_Font10IdentityMatches(
         font10.glyph_count,
         font10.size,
         font10.payload_crc32,
         font10.cell_width,
         font10.cell_height,
         (int8_t)font10.ascent,
         (int8_t)font10.descent) ||
#endif
      !PalEngineBridge_SetNorPackConst(
         (const uint8_t *)nor_image, nor_pack_size))
   {
      esp_partition_munmap(pal_engine_nor_mmap_handle);
      pal_engine_nor_mmap_handle = 0;
      ESP_LOGE(TAG, "NOR pack or generated FONT10 validation failed");
      return false;
   }
#endif

#if !defined(PAL_EXTREME_CHAPTER_CACHE)
   if (!PalTarget_MountTf())
   {
      ESP_LOGE(TAG, "TF mount failed");
      return false;
   }
#endif
   PalTarget_PrepareTfAccess();
   if (!pal_engine_tf_open &&
      f_open(&pal_engine_tf_file, PAL_ENGINE_TF_PACK_PATH, FA_READ | FA_OPEN_EXISTING) != FR_OK)
   {
      ESP_LOGE(TAG, "TF pack open failed: %s", PAL_ENGINE_TF_PACK_PATH);
      return false;
   }
   pal_engine_tf_open = true;

   if (!PalEngineBridge_SetTfPackReadAt((uint32_t)f_size(&pal_engine_tf_file),
      tf_read_at,
      &pal_engine_tf_file))
   {
      ESP_LOGE(TAG, "TF pack TOC open failed: %s", PAL_ENGINE_TF_PACK_PATH);
      return false;
   }

#if defined(PAL_EXTREME_CHAPTER_CACHE)
   if (!PalEngineChapterCache_TargetInit(
         catalog_image,
         catalog_size,
         core_set_id,
         chapter_overlay_changed,
         NULL))
   {
      ESP_LOGE(TAG, "chapter cache catalog initialization failed");
      return false;
   }
   ESP_LOGI(TAG,
      "engine packs ready: core=%" PRIu32 " tf=%" PRIu32
      " cache=deferred",
      nor_pack_size,
      (uint32_t)f_size(&pal_engine_tf_file));
#else
   ESP_LOGI(TAG, "engine packs ready: nor=%" PRIu32 " tf=%" PRIu32,
      nor_pack_size,
      (uint32_t)f_size(&pal_engine_tf_file));
#endif
   return true;
#endif
}
