#include "SDL.h"

#include "pal_target_board.h"

#include <calico/system/thread.h>
#include <calico/system/tick.h>

Uint32
PalEngineBridge_GetTicks(
   void)
{
   return (Uint32)((tickGetCount() * 1000u) / TICK_FREQ);
}

void
PalEngineBridge_Delay(
   Uint32 ms)
{
   if (ms == 0u)
   {
      threadYield();
      return;
   }
   while (ms > 0u)
   {
      Uint32 slice = ms > 8u ? 8u : ms;

      NdsTarget_AudioPump();
      threadSleep(slice * 1000u);
      ms -= slice;
   }
}
