#include "pal_target_board.h"

#include "global.h"
#include "res.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void
PalEngineBridge_RenderPresent(
   const void *pixels,
   int pitch,
   int w,
   int h)
{
   (void)pixels;
   (void)pitch;
   (void)w;
   (void)h;
}

bool
PalEngineBridge_RenderPresentIndexed(
   const void *pixels,
   int pitch,
   int w,
   int h,
   const void *palette_rgba,
   int region_x,
   int region_y,
   int region_w,
   int region_h)
{
   LPPALMAP map = PAL_GetCurrentMap();
   const EVENTOBJECT *event_objects = NULL;
   unsigned event_object_count = 0u;

   if (pixels == NULL || palette_rgba == NULL || pitch != w ||
      w != (int)PAL_TARGET_LCD_WIDTH ||
      h != (int)PAL_TARGET_LCD_HEIGHT)
   {
      return false;
   }
   (void)region_x;
   (void)region_y;
   (void)region_w;
   (void)region_h;
   if (map != NULL)
   {
      bool scene_transition =
         gpGlobals->fEnteringScene || gpGlobals->fNeedToFadeIn;

      NdsTarget_MinimapSetVisible(!scene_transition);
      if (scene_transition)
      {
         return NdsTarget_FlushIndexedFramebuffer(
            (const uint8_t *)pixels,
            (uint16_t)pitch,
            (const uint8_t *)palette_rgba);
      }
      if (gpGlobals->wNumScene > 0 &&
         gpGlobals->wNumScene < MAX_SCENES)
      {
         unsigned first = gpGlobals->g.rgScene[
            gpGlobals->wNumScene - 1].wEventObjectIndex;
         unsigned end = gpGlobals->g.rgScene[
            gpGlobals->wNumScene].wEventObjectIndex;

         if (first <= end && end <= (unsigned)gpGlobals->g.nEventObject)
         {
            event_objects = gpGlobals->g.lprgEventObject + first;
            event_object_count = end - first;
         }
      }
      NdsTarget_MinimapSetMap(
         map->iMapNum,
         gpGlobals->wNumScene,
         (const uint32_t *)map->Tiles,
         event_objects,
         event_object_count,
         PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset),
         PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset));
   }
   return NdsTarget_FlushIndexedFramebuffer(
      (const uint8_t *)pixels,
      (uint16_t)pitch,
      (const uint8_t *)palette_rgba);
}

void
PalEngineBridge_NotifyPaletteChanged(
   void
)
{
   /* The NDS path consumes the palette during its next indexed flush. */
}
