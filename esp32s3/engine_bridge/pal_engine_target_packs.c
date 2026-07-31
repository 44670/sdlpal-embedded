#include "pal_engine_pack_provider.h"

#include "pal_target_board.h"
#if defined(PAL_CARDPUTER_EXTREME) || defined(PAL_EXTREME_CHAPTER_CACHE)
#include "../../embedded/pal_font10_cache.h"
#include "../../embedded/pal_ui_layout_runtime.h"
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
#define PAL_ENGINE_TF_PACK_PATH "0:/pal_tf.pak"

static const char *TAG = "pal_engine_packs";
static FIL pal_engine_tf_file;
static bool pal_engine_tf_open;
static esp_partition_mmap_handle_t pal_engine_nor_mmap_handle;
static uint8_t pal_sram_engine_pack_header[PAL_ENGINE_PACK_HEADER_BYTES];

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
   uint32_t nor_pack_size;
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   PalPack core_pack;
   PalPackSpan catalog_span;
   PalFont10Cache font10;
   uint32_t core_set_id;
#elif defined(PAL_CARDPUTER_EXTREME)
   PalPack nor_pack;
   PalFont10Cache font10;
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
   core_set_id = read_le32(
      pal_sram_engine_pack_header + PAL_ENGINE_PACK_SET_ID_OFFSET);
   if (core_set_id == 0u)
   {
      ESP_LOGE(TAG, "invalid core pack header");
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
      !PalUiLayout_Font10IdentityMatches(
         font10.glyph_count,
         font10.size,
         font10.payload_crc32,
         font10.cell_width,
         font10.cell_height,
         (int8_t)font10.ascent,
         (int8_t)font10.descent) ||
      !PalPack_MapConst(&core_pack,
         PAL_ENGINE_CACHE_CATALOG_ARCHIVE, 0u, &catalog_span) ||
      !PalEngineBridge_SetCorePackConst(
         (const uint8_t *)nor_image, nor_pack_size))
   {
      esp_partition_munmap(pal_engine_nor_mmap_handle);
      pal_engine_nor_mmap_handle = 0;
      ESP_LOGE(TAG,
         "core pack, generated FONT10, or chapter catalog validation failed");
      return false;
   }
#else
   if (
#if defined(PAL_CARDPUTER_EXTREME)
      !PalPack_OpenConst(
         &nor_pack, (const uint8_t *)nor_image, nor_pack_size) ||
      !PalFont10_Open(&nor_pack, &font10) ||
      !PalUiLayout_Font10IdentityMatches(
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
      nor_pack_size,
      (uint32_t)f_size(&pal_engine_tf_file));
#else
   ESP_LOGI(TAG, "engine packs ready: nor=%" PRIu32 " tf=%" PRIu32,
      nor_pack_size,
      (uint32_t)f_size(&pal_engine_tf_file));
#endif
   return true;
}
