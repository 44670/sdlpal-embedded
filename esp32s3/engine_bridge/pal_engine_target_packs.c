#include "pal_engine_pack_provider.h"

#include "pal_target_board.h"
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

#define PAL_ENGINE_NOR_PARTITION_SUBTYPE 0x40
#if defined(PAL_EXTREME_CHAPTER_CACHE)
#define PAL_ENGINE_NOR_PARTITION_LABEL "pal_core"
#define PAL_ENGINE_CACHE_CATALOG_ARCHIVE 20u
#define PAL_ENGINE_PACK_SET_ID_OFFSET 20u
#define PAL_ENGINE_PACK_SIZE_OFFSET 24u
#else
#define PAL_ENGINE_NOR_PARTITION_LABEL "pal_nor"
#endif
#define PAL_ENGINE_TF_PACK_PATH "0:/pal_tf.pak"

static const char *TAG = "pal_engine_packs";
static FIL pal_engine_tf_file;
static bool pal_engine_tf_open;
static esp_partition_mmap_handle_t pal_engine_nor_mmap_handle;

#if defined(PAL_EXTREME_CHAPTER_CACHE)
static uint8_t pal_sram_core_pack_header[32];

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

bool
PalEngineBridge_TargetInitPacks(
   void
)
{
   const esp_partition_t *partition;
   const void *nor_image = NULL;
   esp_err_t err;
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   PalPack core_pack;
   PalPackSpan catalog_span;
   uint32_t core_pack_size;
   uint32_t core_set_id;
#endif

   PalEngineBridge_ClearPacks();
   partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      PAL_ENGINE_NOR_PARTITION_SUBTYPE,
      PAL_ENGINE_NOR_PARTITION_LABEL);
   if (partition == NULL)
   {
      ESP_LOGE(TAG, "missing NOR partition: %s", PAL_ENGINE_NOR_PARTITION_LABEL);
      return false;
   }

#if defined(PAL_EXTREME_CHAPTER_CACHE)
   if (partition->size < sizeof(pal_sram_core_pack_header) ||
      esp_partition_read(partition, 0u, pal_sram_core_pack_header,
         sizeof(pal_sram_core_pack_header)) != ESP_OK)
   {
      ESP_LOGE(TAG, "core pack header read failed");
      return false;
   }
   core_pack_size = read_le32(
      pal_sram_core_pack_header + PAL_ENGINE_PACK_SIZE_OFFSET);
   core_set_id = read_le32(
      pal_sram_core_pack_header + PAL_ENGINE_PACK_SET_ID_OFFSET);
   if (core_pack_size < sizeof(pal_sram_core_pack_header) ||
      core_pack_size > partition->size || core_set_id == 0u)
   {
      ESP_LOGE(TAG, "invalid core pack header");
      return false;
   }
   err = esp_partition_mmap(partition,
      0, core_pack_size, ESP_PARTITION_MMAP_DATA,
      &nor_image, &pal_engine_nor_mmap_handle);
#else
   err = esp_partition_mmap(partition,
      0, partition->size, ESP_PARTITION_MMAP_DATA,
      &nor_image, &pal_engine_nor_mmap_handle);
#endif
   if (err != ESP_OK)
   {
      ESP_LOGE(TAG, "NOR pack open failed: %s", esp_err_to_name(err));
      return false;
   }
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   if (!PalPack_OpenConst(&core_pack,
         (const uint8_t *)nor_image, core_pack_size) ||
      !PalPack_MapConst(&core_pack,
         PAL_ENGINE_CACHE_CATALOG_ARCHIVE, 0u, &catalog_span) ||
      !PalEngineBridge_SetCorePackConst(
         (const uint8_t *)nor_image, core_pack_size))
   {
      esp_partition_munmap(pal_engine_nor_mmap_handle);
      pal_engine_nor_mmap_handle = 0;
      ESP_LOGE(TAG, "core pack or chapter catalog open failed");
      return false;
   }
#else
   if (!PalEngineBridge_SetNorPackConst(
         (const uint8_t *)nor_image, partition->size))
   {
      ESP_LOGE(TAG, "NOR pack validation failed");
      return false;
   }
#endif

   if (!PalTarget_MountTf())
   {
      ESP_LOGE(TAG, "TF mount failed");
      return false;
   }
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
         catalog_span.data,
         catalog_span.size,
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
      core_pack_size,
      (uint32_t)f_size(&pal_engine_tf_file));
#else
   ESP_LOGI(TAG, "engine packs ready: nor=%" PRIu32 " tf=%" PRIu32,
      partition->size,
      (uint32_t)f_size(&pal_engine_tf_file));
#endif
   return true;
}
