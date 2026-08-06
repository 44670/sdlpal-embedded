#include "pal_engine_guru.h"

#include "pal_target_board.h"

#include <stdbool.h>
#include <stdint.h>

void AUDIO_CloseDevice(void);

void
PalEngineBridge_GuruMeditationAt(
   const char *file,
   uint32_t line,
   const char *reason)
{
   static bool entering;

   if (!entering)
   {
      entering = true;
      AUDIO_CloseDevice();
   }
   NdsTarget_FatalAt(file, line, reason);
}
