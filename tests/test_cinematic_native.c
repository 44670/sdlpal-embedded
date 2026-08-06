#include "palcommon.h"
#include "embedded/pal_fullscreen_stretch.h"
#include "esp32s3/engine_bridge/pal_engine_pack_provider.h"
#include "pal_target_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
   TEST_GUARD = 32,
   TEST_MAX_PITCH = 263,
   TEST_MAX_HEIGHT = 192
};

uint8_t pal_sram_fbp_scanline[PAL_EXTREME_FBP_SCANLINE_BYTES];

static uint8_t test_rle[1024];
static uint8_t test_fbp[320u * 200u];
static uint8_t test_surface_storage[
   TEST_GUARD + TEST_MAX_PITCH * TEST_MAX_HEIGHT + TEST_GUARD];

int
PalEngineBridge_GetNativeChunkSize(
   FILE     *fp,
   uint16_t  chunk_id
)
{
   return fp == (FILE *)(uintptr_t)1u && chunk_id == 7u ?
      (int)sizeof(test_fbp) : -1;
}

bool
PalEngineBridge_ReadNativeChunkRange(
   FILE        *fp,
   uint16_t     chunk_id,
   uint32_t     chunk_offset,
   uint8_t     *dst,
   uint32_t     size
)
{
   if (fp != (FILE *)(uintptr_t)1u || chunk_id != 7u || dst == NULL ||
      chunk_offset > sizeof(test_fbp) ||
      size > sizeof(test_fbp) - chunk_offset)
   {
      return false;
   }
   memcpy(dst, test_fbp + chunk_offset, size);
   return true;
}

static size_t
build_test_rle(
   void
)
{
   size_t offset = 0u;
   uint32_t y;

   test_rle[offset++] = 40u;
   test_rle[offset++] = 0u;
   test_rle[offset++] = 20u;
   test_rle[offset++] = 0u;
   for (y = 0u; y < 20u; y++)
   {
      uint32_t x;

      test_rle[offset++] = 0x85u;
      test_rle[offset++] = 30u;
      for (x = 0u; x < 30u; x++)
      {
         test_rle[offset++] = (uint8_t)(0x10u + (y * 30u + x) % 0x70u);
      }
      test_rle[offset++] = 0x85u;
   }
   return offset;
}

static int
test_rle_case(
   int      width,
   int      height,
   PAL_POS  position,
   int      visible_height
)
{
   SDL_Surface surface;
   int pitch = width + 7;
   uint8_t *pixels = test_surface_storage + TEST_GUARD;
   int x;
   int y;

   if (pitch > TEST_MAX_PITCH || height > TEST_MAX_HEIGHT)
   {
      return 1;
   }
   memset(test_surface_storage, 0xa5, sizeof(test_surface_storage));
   for (y = 0; y < height; y++)
   {
      memset(pixels + y * pitch, 0xee, (size_t)width);
      memset(pixels + y * pitch + width, 0xcc, (size_t)(pitch - width));
   }
   memset(&surface, 0, sizeof(surface));
   surface.w = width;
   surface.h = height;
   surface.pitch = pitch;
   surface.pixels = pixels;

   if (PAL_RLEBlitToSurfaceFullCanvas(test_rle, &surface,
         position, visible_height) != 0)
   {
      return 2;
   }
   for (y = 0; y < height; y++)
   {
      uint32_t canvas_y = 0u;

      (void)PalFullScreenStretch_SourceCoordinate((uint32_t)y,
         (uint32_t)height, 200u, &canvas_y);
      for (x = 0; x < width; x++)
      {
         uint32_t canvas_x = 0u;
         int local_x;
         int local_y;
         uint8_t expected = 0xeeu;

         (void)PalFullScreenStretch_SourceCoordinate((uint32_t)x,
            (uint32_t)width, 320u, &canvas_x);
         local_x = (int)canvas_x - PAL_X(position);
         local_y = (int)canvas_y - PAL_Y(position);
         if (local_x >= 5 && local_x < 35 && local_y >= 0 &&
            local_y < visible_height && local_y < 20)
         {
            expected = (uint8_t)(0x10u +
               ((uint32_t)local_y * 30u + (uint32_t)(local_x - 5)) %
                  0x70u);
         }
         if (pixels[y * pitch + x] != expected)
         {
            return 3;
         }
      }
      for (; x < pitch; x++)
      {
         if (pixels[y * pitch + x] != 0xccu)
         {
            return 4;
         }
      }
   }
   for (x = 0; x < TEST_GUARD; x++)
   {
      if (test_surface_storage[x] != 0xa5u ||
         test_surface_storage[TEST_GUARD + pitch * height + x] != 0xa5u)
      {
         return 5;
      }
   }
   return 0;
}

static uint8_t
incoming_pixel(
   uint32_t x,
   uint32_t y,
   uint32_t width,
   uint32_t height
)
{
   uint32_t source_x = 0u;
   uint32_t source_y = 0u;

   (void)PalFullScreenStretch_SourceCoordinate(x, width, 320u, &source_x);
   (void)PalFullScreenStretch_SourceCoordinate(y, height, 200u, &source_y);
   return test_fbp[source_y * 320u + source_x];
}

static uint8_t
outgoing_pixel(
   uint32_t x,
   uint32_t y
)
{
   return (uint8_t)(0x80u ^ (y * 17u + x * 3u));
}

static int
check_transition_state(
   const SDL_Surface *surface,
   uint32_t           progress,
   BOOL               scroll_down
)
{
   uint32_t width = (uint32_t)surface->w;
   uint32_t height = (uint32_t)surface->h;
   uint32_t offset = progress == 200u ? height :
      PalFullScreenStretch_LowerBound(progress, 200u, height);
   const uint8_t *pixels = (const uint8_t *)surface->pixels;
   uint32_t x;
   uint32_t y;

   for (y = 0u; y < height; y++)
   {
      for (x = 0u; x < width; x++)
      {
         uint8_t expected;

         if (scroll_down)
         {
            expected = y < offset ?
               incoming_pixel(x, height - offset + y, width, height) :
               outgoing_pixel(x, y - offset);
         }
         else
         {
            expected = y >= height - offset ?
               incoming_pixel(x, y - (height - offset), width, height) :
               outgoing_pixel(x, y + offset);
         }
         if (pixels[y * (uint32_t)surface->pitch + x] != expected)
         {
            return 1;
         }
      }
      for (; x < (uint32_t)surface->pitch; x++)
      {
         if (pixels[y * (uint32_t)surface->pitch + x] != 0xccu)
         {
            return 2;
         }
      }
   }
   return 0;
}

static int
test_transition_case(
   int   width,
   int   height,
   BOOL  scroll_down
)
{
   static const UINT progress_points[] =
      {0u, 1u, 2u, 17u, 50u, 51u, 99u, 100u, 149u, 199u, 200u};
   SDL_Surface surface;
   int pitch = width + 7;
   uint8_t *pixels = test_surface_storage + TEST_GUARD;
   UINT previous = 0u;
   size_t i;
   int x;
   int y;

   memset(test_surface_storage, 0xa5, sizeof(test_surface_storage));
   for (y = 0; y < height; y++)
   {
      for (x = 0; x < width; x++)
      {
         pixels[y * pitch + x] = outgoing_pixel((uint32_t)x, (uint32_t)y);
      }
      memset(pixels + y * pitch + width, 0xcc, (size_t)(pitch - width));
   }
   memset(&surface, 0, sizeof(surface));
   surface.w = width;
   surface.h = height;
   surface.pitch = pitch;
   surface.pixels = pixels;

   for (i = 0u; i < sizeof(progress_points) / sizeof(progress_points[0]); i++)
   {
      UINT progress = progress_points[i];

      if (PAL_FBPAdvanceChunkVerticalTransition(
            (FILE *)(uintptr_t)1u, 7u, &surface,
            previous, progress, scroll_down) != 0 ||
         check_transition_state(&surface, progress, scroll_down) != 0)
      {
         return 1;
      }
      previous = progress;
   }
   if (PAL_FBPAdvanceChunkVerticalTransition(
         (FILE *)(uintptr_t)1u, 7u, &surface,
         2u, 1u, scroll_down) == 0 ||
      PAL_FBPAdvanceChunkVerticalTransition(
         (FILE *)(uintptr_t)1u, 7u, &surface,
         200u, 201u, scroll_down) == 0)
   {
      return 2;
   }
   for (x = 0; x < TEST_GUARD; x++)
   {
      if (test_surface_storage[x] != 0xa5u ||
         test_surface_storage[TEST_GUARD + pitch * height + x] != 0xa5u)
      {
         return 3;
      }
   }
   return 0;
}

int
main(
   void
)
{
   uint32_t x;
   uint32_t y;

   (void)build_test_rle();
   for (y = 0u; y < 200u; y++)
   {
      for (x = 0u; x < 320u; x++)
      {
         test_fbp[y * 320u + x] = (uint8_t)(y * 13u + x * 7u);
      }
   }
   if (test_rle_case(240, 135, PAL_XY(100, 50), 20) != 0 ||
      test_rle_case(160, 128, PAL_XY(100, 50), 10) != 0 ||
      test_rle_case(256, 192, PAL_XY(0, -10), 20) != 0 ||
      test_transition_case(240, 135, TRUE) != 0 ||
      test_transition_case(240, 135, FALSE) != 0 ||
      test_transition_case(160, 128, TRUE) != 0 ||
      test_transition_case(160, 128, FALSE) != 0 ||
      test_transition_case(256, 192, TRUE) != 0 ||
      test_transition_case(256, 192, FALSE) != 0)
   {
      return 1;
   }
   puts("two-screen cinematic renderer test passed");
   return 0;
}
