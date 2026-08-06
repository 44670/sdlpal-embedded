#include "pal_engine_pack_provider.h"

#include "pal_target_board.h"
#include "pal_memory_profile.h"
#if defined(PAL_STORAGE_SD_ONLY)
#include "../../embedded/pal_level2_resident_pack.h"
#endif
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
#include <limits.h>
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
#if !defined(PAL_STORAGE_SD_ONLY)
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
   FRESULT result;
   FSIZE_t position;

   if (file == NULL || (dst == NULL && size != 0u))
   {
      return false;
   }
   if (size > (uint32_t)UINT_MAX)
   {
      ESP_LOGE(TAG, "TF read size exceeds FatFS UINT: offset=%" PRIu32
         " size=%" PRIu32, offset, size);
      return false;
   }
   PalTarget_PrepareTfAccess();
   result = f_lseek(file, (FSIZE_t)offset);
   if (result != FR_OK)
   {
      ESP_LOGE(TAG, "TF seek failed: offset=%" PRIu32 " size=%" PRIu32
         " dst=%p dst_mod4=%u offset_mod512=%u result=%d file_size=%" PRIuMAX,
         offset, size, (void *)dst, (unsigned)((uintptr_t)dst & 3u),
         (unsigned)(offset & 511u), (int)result, (uintmax_t)f_size(file));
      return false;
   }
   result = f_read(file, dst, (UINT)size, &got);
   position = f_tell(file);
   if (result != FR_OK)
   {
      ESP_LOGE(TAG, "TF read error: offset=%" PRIu32 " request=%" PRIu32
         " got=%u position=%" PRIuMAX " dst=%p dst_mod4=%u "
         "request_mod4=%u offset_mod512=%u result=%d file_size=%" PRIuMAX,
         offset, size, (unsigned)got, (uintmax_t)position,
         (void *)dst, (unsigned)((uintptr_t)dst & 3u),
         (unsigned)(size & 3u), (unsigned)(offset & 511u), (int)result,
         (uintmax_t)f_size(file));
      return false;
   }
   if (got != (UINT)size)
   {
      ESP_LOGE(TAG, "TF short read: offset=%" PRIu32 " request=%" PRIu32
         " got=%u position=%" PRIuMAX " dst=%p dst_mod4=%u "
         "request_mod4=%u offset_mod512=%u file_size=%" PRIuMAX, offset,
         size, (unsigned)got, (uintmax_t)position, (void *)dst,
         (unsigned)((uintptr_t)dst & 3u), (unsigned)(size & 3u),
         (unsigned)(offset & 511u), (uintmax_t)f_size(file));
      return false;
   }
   return true;
}

#if defined(PAL_STORAGE_SD_ONLY)
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
      !PalLevel2ResidentPack_Build(
         &full_toc, PAL_LEVEL2_RESIDENT_DEFAULT_MASK,
         tf_read_at, &pal_engine_tf_file,
         pal_mem_level2_resident_pack,
         PAL_MEM_LEVEL2_RESIDENT_PACK_BYTES, &core_size) ||
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
      font10.cell_width != 10u || font10.cell_height != 10u ||
#endif
      !PalEngineBridge_SetNorPackConst(
         (const uint8_t *)nor_image, nor_pack_size))
   {
      esp_partition_munmap(pal_engine_nor_mmap_handle);
      pal_engine_nor_mmap_handle = 0;
      ESP_LOGE(TAG, "NOR pack or FONT10 geometry validation failed");
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
