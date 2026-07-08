#include "cores3se_board.h"
#include "cores3se_memory.h"
#include "pal_engine_pack_provider.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

int PAL_EngineMain(int argc, char *argv[]);

void
app_main(
   void
)
{
   char arg0[] = "sdlpal";
   char *argv[] = { arg0, 0 };

   if (!CoreS3Se_Begin())
   {
      CoreS3Se_ShowError("BOARD FAIL", "CORES3SE INIT");
      for (;;)
      {
         vTaskDelay(pdMS_TO_TICKS(1000));
      }
   }
   CoreS3Se_TouchReservedBuffers();

   if (!PalEngineBridge_TargetInitPacks())
   {
      CoreS3Se_ShowError("PACK FAIL", "NOR OR TF");
      for (;;)
      {
         vTaskDelay(pdMS_TO_TICKS(1000));
      }
   }

   (void)PAL_EngineMain(1, argv);
   for (;;)
   {
      vTaskDelay(pdMS_TO_TICKS(1000));
   }
}
