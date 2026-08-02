#include "main.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int
run_surface_test(
   int width,
   int height
)
{
   enum { GUARD = 32, MAX_PIXELS = 400 * 240 };
   uint8_t storage[GUARD + MAX_PIXELS + GUARD];
   const uint8_t frame[] = { 0x12, 0xff, 0x7c, 0x2a, 0x7b, 0x00 };
   SDL_Surface surface;
   int x;
   int y;

   memset(storage, 0xa5, sizeof(storage));
   memset(storage + GUARD, 0, (size_t)width * (size_t)height);
   memset(&surface, 0, sizeof(surface));
   surface.w = width;
   surface.h = height;
   surface.pitch = width;
   surface.pixels = storage + GUARD;

   if (PAL_RNGBlitToSurfaceForTest(frame, sizeof(frame), &surface) != 0)
   {
      return 1;
   }
   for (x = 0; x < GUARD; x++)
   {
      if (storage[x] != 0xa5 ||
         storage[GUARD + width * height + x] != 0xa5)
      {
         return 2;
      }
   }
   for (y = 0; y < height; y++)
   {
      uint32_t source_y = ((uint32_t)(2 * y + 1) * 200u) /
         ((uint32_t)height * 2u);
      for (x = 0; x < width; x++)
      {
         uint32_t source_x = ((uint32_t)(2 * x + 1) * 320u) /
            ((uint32_t)width * 2u);
         uint8_t expected = ((source_y * 320u + source_x) & 1u) ?
            0x7b : 0x2a;
         if (storage[GUARD + y * width + x] != expected)
         {
            return 3;
         }
      }
   }
   return 0;
}

typedef struct tagSTREAMCONTEXT
{
   const uint8_t *data;
   uint32_t length;
   uint32_t calls;
   uint32_t max_request;
} STREAMCONTEXT;

static BOOL
stream_read_at(
   void     *user,
   uint32_t  offset,
   uint8_t  *destination,
   uint32_t  size
)
{
   STREAMCONTEXT *context = (STREAMCONTEXT *)user;

   if (context == NULL || destination == NULL ||
      offset > context->length || size > context->length - offset)
   {
      return FALSE;
   }
   memcpy(destination, context->data + offset, size);
   context->calls++;
   if (size > context->max_request)
   {
      context->max_request = size;
   }
   return TRUE;
}

static int
run_stream_test(
   void
)
{
   const uint8_t frame[] = { 0x12, 0xff, 0x7c, 0x2a, 0x7b, 0x00 };
   uint8_t reference[16 * 8];
   uint8_t streamed[16 * 8];
   uint8_t window[3];
   SDL_Surface reference_surface;
   SDL_Surface streamed_surface;
   uint32_t capacity;

   memset(&reference_surface, 0, sizeof(reference_surface));
   reference_surface.w = 16;
   reference_surface.h = 8;
   reference_surface.pitch = 16;
   reference_surface.pixels = reference;
   memset(reference, 0, sizeof(reference));
   if (PAL_RNGBlitToSurfaceForTest(frame, sizeof(frame),
         &reference_surface) != 0)
   {
      return 1;
   }

   memset(&streamed_surface, 0, sizeof(streamed_surface));
   streamed_surface.w = 16;
   streamed_surface.h = 8;
   streamed_surface.pitch = 16;
   streamed_surface.pixels = streamed;
   for (capacity = 1u; capacity <= sizeof(window); capacity++)
   {
      STREAMCONTEXT context = {
         frame, sizeof(frame), 0u, 0u
      };

      memset(streamed, 0, sizeof(streamed));
      if (PAL_RNGBlitReadAtToSurfaceForTest(stream_read_at, &context,
            sizeof(frame), window, capacity, &streamed_surface) != 0 ||
         memcmp(reference, streamed, sizeof(reference)) != 0 ||
         context.calls < 2u || context.max_request > capacity)
      {
         return 2;
      }
   }
   return 0;
}

static int
run_opcode_test(
   void
)
{
   uint8_t pixels[320 * 200];
   const uint8_t frame[] = {
      0x02,
      0x03, 0x00,
      0x04, 0x00, 0x00,
      0x06, 0x01, 0x02,
      0x07, 0x03, 0x04, 0x05, 0x06,
      0x0a, 0x07, 0x08, 0x09, 0x0a, 0x0b,
            0x0c, 0x0d, 0x0e, 0x0f, 0x10,
      0x0b, 0x01, 0x11, 0x12, 0x13, 0x14,
      0x0c, 0x00, 0x00, 0x15, 0x16,
      0x0d, 0x17, 0x18,
      0x10, 0x19, 0x1a,
      0x11, 0x00, 0x1b, 0x1c,
      0x12, 0x00, 0x00, 0x1d, 0x1e,
      0x00,
   };
   SDL_Surface surface;
   int i;

   memset(pixels, 0xcc, sizeof(pixels));
   memset(&surface, 0, sizeof(surface));
   surface.w = 320;
   surface.h = 200;
   surface.pitch = 320;
   surface.pixels = pixels;
   if (PAL_RNGBlitToSurfaceForTest(frame, sizeof(frame), &surface) != 0)
   {
      return 1;
   }
   for (i = 0; i < 6; i++)
   {
      if (pixels[i] != 0xcc)
      {
         return 2;
      }
   }
   for (i = 6; i < 28; i++)
   {
      if (pixels[i] != (uint8_t)(i - 5))
      {
         return 3;
      }
   }
   for (i = 28; i < 32; i++)
   {
      if (pixels[i] != ((i & 1) ? 0x18 : 0x17))
      {
         return 4;
      }
   }
   for (i = 32; i < 42; i++)
   {
      if (pixels[i] != ((i & 1) ? 0x1a : 0x19))
      {
         return 5;
      }
   }
   if (pixels[42] != 0x1b || pixels[43] != 0x1c ||
      pixels[44] != 0x1d || pixels[45] != 0x1e ||
      pixels[46] != 0xcc)
   {
      return 6;
   }
   return 0;
}

int
main(
   void
)
{
   uint8_t pixels[16];
   SDL_Surface surface;
   const uint8_t truncated[] = { 0x0b, 0x01, 0x55 };
   const uint8_t oversized_skip[] = { 0x04, 0xff, 0xff };

   if (run_surface_test(240, 135) != 0 ||
      run_surface_test(160, 128) != 0 ||
      run_surface_test(320, 200) != 0 ||
      run_surface_test(400, 240) != 0 ||
      run_opcode_test() != 0 ||
      run_stream_test() != 0)
   {
      return 1;
   }

   memset(&surface, 0, sizeof(surface));
   surface.w = 4;
   surface.h = 4;
   surface.pitch = 4;
   surface.pixels = pixels;
   memset(pixels, 0xcc, sizeof(pixels));
   if (PAL_RNGBlitToSurfaceForTest(
         truncated, sizeof(truncated), &surface) != -1 ||
      PAL_RNGBlitToSurfaceForTest(
         oversized_skip, sizeof(oversized_skip), &surface) != -1)
   {
      return 2;
   }

   puts("RNG native decoder test passed");
   return 0;
}
