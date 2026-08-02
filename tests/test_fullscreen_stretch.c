#include "embedded/pal_fullscreen_stretch.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int
test_inverse_mapping(
   void
)
{
   uint32_t source_extent;
   uint32_t destination_extent;

   for (source_extent = 1u; source_extent <= 64u; source_extent++)
   {
      for (destination_extent = 1u;
         destination_extent <= 64u; destination_extent++)
      {
         uint32_t source;

         for (source = 0u; source < source_extent; source++)
         {
            uint32_t expected_first = destination_extent;
            uint32_t expected_after = destination_extent;
            uint32_t first = 0u;
            uint32_t after = 0u;
            uint32_t destination;
            bool found;

            for (destination = 0u;
               destination < destination_extent; destination++)
            {
               uint32_t mapped = 0u;

               if (!PalFullScreenStretch_SourceCoordinate(destination,
                     destination_extent, source_extent, &mapped))
               {
                  return 1;
               }
               if (mapped == source)
               {
                  if (expected_first == destination_extent)
                  {
                     expected_first = destination;
                  }
                  expected_after = destination + 1u;
               }
            }
            found = PalFullScreenStretch_DestinationRange(source,
               source_extent, destination_extent, &first, &after);
            if (expected_first == destination_extent)
            {
               if (found || first != 0u || after != 0u)
               {
                  return 2;
               }
            }
            else if (!found || first != expected_first ||
               after != expected_after)
            {
               return 3;
            }
         }
      }
   }
   return 0;
}

static int
test_blit_case(
   uint32_t destination_width,
   uint32_t destination_height
)
{
   enum {
      SOURCE_WIDTH = 7,
      SOURCE_HEIGHT = 5,
      SOURCE_PITCH = 11,
      DESTINATION_PITCH = 29,
      DESTINATION_ROWS = 19,
      GUARD = 32
   };
   uint8_t source[SOURCE_PITCH * SOURCE_HEIGHT];
   uint8_t storage[GUARD + DESTINATION_PITCH * DESTINATION_ROWS + GUARD];
   uint8_t *destination = storage + GUARD;
   uint32_t x;
   uint32_t y;

   if (destination_width > DESTINATION_PITCH ||
      destination_height > DESTINATION_ROWS)
   {
      return 1;
   }
   memset(source, 0xee, sizeof(source));
   for (y = 0u; y < SOURCE_HEIGHT; y++)
   {
      for (x = 0u; x < SOURCE_WIDTH; x++)
      {
         source[y * SOURCE_PITCH + x] = (uint8_t)(y * 16u + x);
      }
   }
   memset(storage, 0xa5, sizeof(storage));
   if (!PalFullScreenStretch_BlitIndexed(source,
         SOURCE_WIDTH, SOURCE_HEIGHT, SOURCE_PITCH,
         destination, destination_width, destination_height,
         DESTINATION_PITCH))
   {
      return 2;
   }
   for (y = 0u; y < destination_height; y++)
   {
      uint32_t source_y = 0u;

      (void)PalFullScreenStretch_SourceCoordinate(y, destination_height,
         SOURCE_HEIGHT, &source_y);
      for (x = 0u; x < destination_width; x++)
      {
         uint32_t source_x = 0u;
         uint8_t expected;

         (void)PalFullScreenStretch_SourceCoordinate(x, destination_width,
            SOURCE_WIDTH, &source_x);
         expected = source[source_y * SOURCE_PITCH + source_x];
         if (destination[y * DESTINATION_PITCH + x] != expected)
         {
            return 3;
         }
      }
      for (; x < DESTINATION_PITCH; x++)
      {
         if (destination[y * DESTINATION_PITCH + x] != 0xa5)
         {
            return 4;
         }
      }
   }
   for (x = 0u; x < GUARD; x++)
   {
      if (storage[x] != 0xa5 ||
         storage[GUARD + DESTINATION_PITCH * DESTINATION_ROWS + x] != 0xa5)
      {
         return 5;
      }
   }
   return 0;
}

int
main(
   void
)
{
   uint8_t pixel = 0u;
   uint32_t coordinate = 0u;
   uint32_t first = 0u;
   uint32_t after = 0u;

   if (test_inverse_mapping() != 0 ||
      test_blit_case(7u, 5u) != 0 ||
      test_blit_case(3u, 11u) != 0 ||
      test_blit_case(17u, 2u) != 0 ||
      test_blit_case(1u, 1u) != 0)
   {
      return 1;
   }
   if (PalFullScreenStretch_SourceCoordinate(0u, 0u, 1u,
         &coordinate) ||
      PalFullScreenStretch_DestinationRange(1u, 1u, 1u,
         &first, &after) ||
      PalFullScreenStretch_BlitIndexed(NULL, 1u, 1u, 1u,
         &pixel, 1u, 1u, 1u))
   {
      return 2;
   }
   puts("full-screen stretch test passed");
   return 0;
}
