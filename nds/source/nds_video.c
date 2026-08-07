#include "pal_target_board.h"

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

void
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
   if (pixels == NULL || palette_rgba == NULL || pitch != w ||
      w != (int)PAL_TARGET_LCD_WIDTH ||
      h != (int)PAL_TARGET_LCD_HEIGHT)
   {
      return;
   }
   (void)NdsTarget_FlushIndexedFramebuffer(
      (const uint8_t *)pixels,
      (uint16_t)pitch,
      (const uint8_t *)palette_rgba);
   (void)region_x;
   (void)region_y;
   (void)region_w;
   (void)region_h;
}

void
PalEngineBridge_NotifyPaletteChanged(
   void
)
{
   /* The NDS path consumes the palette during its next indexed flush. */
}
