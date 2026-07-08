#include "SDL.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

Uint32
PalEngineBridge_GetTicks(
   void
)
{
   return (Uint32)(esp_timer_get_time() / 1000);
}

void
PalEngineBridge_Delay(
   Uint32 ms
)
{
   if (ms == 0)
   {
      vTaskDelay(0);
      return;
   }
   vTaskDelay(pdMS_TO_TICKS(ms));
}
