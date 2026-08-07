#include "../main/cardputer_extreme_board.h"
#include "../main/cardputer_extreme_native_view.h"
#include "../main/cores3se_board.h"
#if defined(PAL_TARGET_XIAOMIAO)
#include "../main/xiaomiao_board.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t pal_native_cardputer_argb[
   CARDPUTER_EXTREME_LCD_WIDTH * CARDPUTER_EXTREME_LCD_HEIGHT * 4u];
static uint8_t pal_native_cardputer_current_key;
static uint8_t pal_native_cardputer_pending_key;

uint16_t PalNativeHost_LogicalWidth(void);
uint16_t PalNativeHost_LogicalHeight(void);

/*
 * Host-only board adapter for running the exact Cardputer extreme engine
 * profile through the deterministic harness.  Storage/time/FatFS emulation
 * remains in cores3se_native_shim.c; this file only supplies the different
 * board API. The shared scene-mapping math is linked separately so host
 * screenshots exercise the same coordinates as the ESP-IDF presenter.
 */
bool
CardputerExtreme_Begin(
   void
)
{
   return CoreS3Se_Begin();
}

bool
CardputerExtreme_MountTf(
   void
)
{
   return CoreS3Se_MountTf();
}

void
CardputerExtreme_PrepareTfAccess(
   void
)
{
   CoreS3Se_PrepareTfAccess();
}

bool
CardputerExtreme_PollKey(
   uint8_t *ascii,
   bool *pressed
)
{
   uint16_t x = 0;
   uint16_t y = 0;
   uint8_t desired = 0;

   if (ascii == NULL || pressed == NULL)
   {
      return false;
   }
   if (pal_native_cardputer_pending_key != 0)
   {
      pal_native_cardputer_current_key = pal_native_cardputer_pending_key;
      pal_native_cardputer_pending_key = 0;
      *ascii = pal_native_cardputer_current_key;
      *pressed = true;
      return true;
   }
   if (CoreS3Se_TouchPoint(&x, &y))
   {
      if (y < CORES3SE_PAL_Y_OFFSET)
      {
         desired = '`';
      }
      else if (y >= CORES3SE_PAL_Y_OFFSET + 200u)
      {
         desired = '\r';
      }
      else if (y < CORES3SE_PAL_Y_OFFSET + 200u / 3u)
      {
         desired = ';';
      }
      else if (y >= CORES3SE_PAL_Y_OFFSET + 2u * (200u / 3u))
      {
         desired = '.';
      }
      else if (x < CORES3SE_LCD_WIDTH / 3u)
      {
         desired = ',';
      }
      else if (x >= 2u * (CORES3SE_LCD_WIDTH / 3u))
      {
         desired = '/';
      }
      else
      {
         desired = '\r';
      }
   }
   if (desired == pal_native_cardputer_current_key)
   {
      return false;
   }
   if (pal_native_cardputer_current_key != 0)
   {
      *ascii = pal_native_cardputer_current_key;
      *pressed = false;
      pal_native_cardputer_current_key = 0;
      pal_native_cardputer_pending_key = desired;
      return true;
   }
   if (desired != 0)
   {
      pal_native_cardputer_current_key = desired;
      *ascii = desired;
      *pressed = true;
      return true;
   }
   return false;
}

static bool
native_flush_indexed_region(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba,
   uint16_t x,
   uint16_t y,
   uint16_t region_width,
   uint16_t region_height
)
{
   const uint16_t width = PalNativeHost_LogicalWidth();
   const uint16_t height = PalNativeHost_LogicalHeight();
   uint16_t destination_y;

   if (pixels == NULL || palette_rgba == NULL ||
      width == 0u || height == 0u || width > CARDPUTER_EXTREME_LCD_WIDTH ||
      height > CARDPUTER_EXTREME_LCD_HEIGHT || pitch < width ||
      region_width == 0u || region_height == 0u || x >= width || y >= height ||
      region_width > width - x || region_height > height - y)
   {
      return false;
   }
   for (destination_y = y;
      destination_y < (uint16_t)(y + region_height);
      destination_y++)
   {
      uint16_t destination_x;
      for (destination_x = x;
         destination_x < (uint16_t)(x + region_width);
         destination_x++)
      {
         const uint8_t *color;
         uint8_t *destination;
         color = palette_rgba + (size_t)pixels[
            (size_t)destination_y * pitch + destination_x] * 4u;
         destination = pal_native_cardputer_argb +
            ((size_t)destination_y * width +
             destination_x) * 4u;
         destination[0] = color[2];
         destination[1] = color[1];
         destination[2] = color[0];
         destination[3] = 0xffu;
      }
   }
   return CoreS3Se_FlushArgb8888Texture(
      pal_native_cardputer_argb,
      width,
      height,
      width * 4u);
}

bool
CardputerExtreme_FlushIndexedFramebuffer(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba
)
{
   return native_flush_indexed_region(
      pixels, pitch, palette_rgba, 0u, 0u,
      PalNativeHost_LogicalWidth(), PalNativeHost_LogicalHeight());
}

bool
CardputerExtreme_FlushIndexedFramebufferRegion(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba,
   uint16_t x,
   uint16_t y,
   uint16_t width,
   uint16_t height
)
{
   return native_flush_indexed_region(
      pixels, pitch, palette_rgba, x, y, width, height);
}

void
CardputerExtreme_ShowError(
   const char *line1,
   const char *line2
)
{
   fprintf(stderr, "Cardputer extreme error: %s / %s\n",
      line1 != NULL ? line1 : "",
      line2 != NULL ? line2 : "");
}

#if defined(PAL_TARGET_XIAOMIAO)
/*
 * The Linux harness exercises Xiaomiao's exact 160x128 engine/storage path;
 * only physical GPIO/SPI operations remain represented by the existing SDL
 * board adapter.  The ESP-IDF firmware never compiles this translation unit.
 */
bool
Xiaomiao_Begin(
   void
)
{
   return CardputerExtreme_Begin();
}

bool
Xiaomiao_MountTf(
   void
)
{
   return CardputerExtreme_MountTf();
}

bool
Xiaomiao_TfMounted(
   void
)
{
   return true;
}

void
Xiaomiao_PrepareTfAccess(
   void
)
{
   CardputerExtreme_PrepareTfAccess();
}

bool
Xiaomiao_PollKey(
   uint8_t *ascii,
   bool    *pressed
)
{
   return CardputerExtreme_PollKey(ascii, pressed);
}

bool
Xiaomiao_FlushIndexedFramebuffer(
   const uint8_t *pixels,
   uint16_t       pitch,
   const uint8_t *palette_rgba
)
{
   return CardputerExtreme_FlushIndexedFramebuffer(
      pixels, pitch, palette_rgba);
}

bool
Xiaomiao_FlushIndexedFramebufferRegion(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba,
   uint16_t x,
   uint16_t y,
   uint16_t width,
   uint16_t height
)
{
   return CardputerExtreme_FlushIndexedFramebufferRegion(
      pixels, pitch, palette_rgba, x, y, width, height);
}

void
Xiaomiao_ShowError(
   const char *line1,
   const char *line2
)
{
   CardputerExtreme_ShowError(line1, line2);
}
#endif
