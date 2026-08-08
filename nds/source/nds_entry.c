#include "pal_engine_pack_provider.h"
#include "pal_memory_profile.h"
#include "pal_target_board.h"
#include "pal_target_memory.h"
#include "pal_target_save.h"

#include <stdint.h>

int PAL_EngineMain(int argc, char *argv[]);
const char *NdsTarget_PackError(void);

void
PalEngineBridge_LogRuntimeMemory(
   const char *stage)
{
   (void)stage;
}

int
main(
   int argc,
   char **argv)
{
   char arg0[] = "sdlpal";
   char *engine_argv[] = { arg0, 0 };

   (void)argc;
   (void)argv;
   if (!PalTarget_Begin())
   {
      NdsTarget_FatalAt(__FILE__, __LINE__, "video initialization failed");
   }
   NdsTarget_BootLog("touching reserved buffers");
   PalTarget_TouchReservedBuffers();
   PAL_MemoryLevel2Touch();
   NdsTarget_BootLog("resident buffers ok");
   NdsTarget_BootLog("opening pack provider");
   if (!PalEngineBridge_TargetInitPacks())
   {
      NdsTarget_FatalAt(__FILE__, __LINE__, NdsTarget_PackError());
   }
   NdsTarget_BootLog("pack provider ok");
   NdsTarget_ShowReady(
      PalTargetSave_Init(),
      PalTargetSave_Type(),
      PalTargetSave_Capacity());
   (void)PAL_EngineMain(1, engine_argv);
   NdsTarget_FatalAt(__FILE__, __LINE__, "engine returned");
}
