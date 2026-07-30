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
   TickType_t ticks;

   if (ms == 0)
   {
      vTaskDelay(0);
      return;
   }
   ticks = pdMS_TO_TICKS(ms);
   if (ticks == 0)
   {
      /*
       * SDL_Delay() must not turn a positive delay into a zero-tick spin if
       * this target is ever rebuilt with a coarser FreeRTOS tick.
       */
      ticks = 1;
   }
   vTaskDelay(ticks);
}
