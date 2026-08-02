#include "pal_target_board.h"
#include "pal_target_memory.h"
#include "pal_memory_profile.h"
#include "pal_engine_pack_provider.h"
#include "pal_engine_runtime_metrics.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if (defined(MEM_LEVEL1) || defined(MEM_LEVEL2)) && defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <esp_log.h>
#endif

int PAL_EngineMain(int argc, char *argv[]);

void
PalEngineBridge_LogRuntimeMemory(
   const char *stage
)
{
#if (defined(MEM_LEVEL1) || defined(MEM_LEVEL2)) && defined(ESP_PLATFORM)
   const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
   const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
   size_t free_bytes = heap_caps_get_free_size(internal_caps);
   size_t minimum_bytes = heap_caps_get_minimum_free_size(internal_caps);
   size_t largest_bytes = heap_caps_get_largest_free_block(internal_caps);
   size_t dma_free_bytes = heap_caps_get_free_size(dma_caps);
   size_t dma_minimum_bytes = heap_caps_get_minimum_free_size(dma_caps);
   size_t dma_largest_bytes = heap_caps_get_largest_free_block(dma_caps);
   UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);

   ESP_LOGI("pal_extreme_mem",
      "stage=%s internal_free=%u internal_min=%u internal_largest=%u "
      "dma_free=%u dma_min=%u dma_largest=%u main_stack_hwm=%u",
      stage != NULL ? stage : "?",
      (unsigned)free_bytes,
      (unsigned)minimum_bytes,
      (unsigned)largest_bytes,
      (unsigned)dma_free_bytes,
      (unsigned)dma_minimum_bytes,
      (unsigned)dma_largest_bytes,
      (unsigned)stack_high_water);
#else
   (void)stage;
#endif
}

void
app_main(
   void
)
{
   char arg0[] = "sdlpal";
   char *argv[] = { arg0, 0 };

   PalEngineBridge_LogRuntimeMemory("app-entry");
   if (!PalTarget_Begin())
   {
      PalTarget_ShowError("BOARD FAIL", "TARGET INIT");
      for (;;)
      {
         vTaskDelay(pdMS_TO_TICKS(1000));
      }
   }
   PalTarget_TouchReservedBuffers();
#if defined(MEM_LEVEL2)
   PAL_MemoryLevel2Touch();
#endif
   PalEngineBridge_LogRuntimeMemory("board-ready");

   if (!PalEngineBridge_TargetInitPacks())
   {
      PalTarget_ShowError("PACK FAIL", "RESOURCE STORAGE");
      for (;;)
      {
         vTaskDelay(pdMS_TO_TICKS(1000));
      }
   }
   PalEngineBridge_LogRuntimeMemory("packs-ready");

   (void)PAL_EngineMain(1, argv);
   PalEngineBridge_LogRuntimeMemory("engine-returned");
   for (;;)
   {
      vTaskDelay(pdMS_TO_TICKS(1000));
   }
}
