#include "pal_engine_pack_provider.h"

#include "cores3se_board.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <ff.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>

#define PAL_ENGINE_NOR_PARTITION_SUBTYPE 0x40
#define PAL_ENGINE_NOR_PARTITION_LABEL "pal_nor"
#define PAL_ENGINE_TF_PACK_PATH "0:/pal_tf.pak"

static const char *TAG = "pal_engine_packs";
static FIL pal_engine_tf_file;
static bool pal_engine_tf_open;
static esp_partition_mmap_handle_t pal_engine_nor_mmap_handle;

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
   CoreS3Se_PrepareTfAccess();
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

   PalEngineBridge_ClearPacks();
   partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      PAL_ENGINE_NOR_PARTITION_SUBTYPE,
      PAL_ENGINE_NOR_PARTITION_LABEL);
   if (partition == NULL)
   {
      ESP_LOGE(TAG, "missing NOR partition: %s", PAL_ENGINE_NOR_PARTITION_LABEL);
      return false;
   }

   err = esp_partition_mmap(partition,
      0,
      partition->size,
      ESP_PARTITION_MMAP_DATA,
      &nor_image,
      &pal_engine_nor_mmap_handle);
   if (err != ESP_OK || !PalEngineBridge_SetNorPackConst((const uint8_t *)nor_image, partition->size))
   {
      ESP_LOGE(TAG, "NOR pack open failed: %s", esp_err_to_name(err));
      return false;
   }

   if (!CoreS3Se_MountTf())
   {
      ESP_LOGE(TAG, "TF mount failed");
      return false;
   }
   CoreS3Se_PrepareTfAccess();
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

   ESP_LOGI(TAG, "engine packs ready: nor=%" PRIu32 " tf=%" PRIu32,
      partition->size,
      (uint32_t)f_size(&pal_engine_tf_file));
   return true;
}
