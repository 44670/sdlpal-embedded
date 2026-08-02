#ifndef PAL_FULLSCREEN_STRETCH_H
#define PAL_FULLSCREEN_STRETCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Full-canvas indexed materials fill the destination.  X and Y are sampled
 * independently at pixel centres, so this deliberately does not preserve the
 * source aspect ratio.  This header has no SDL or allocation dependency and
 * is shared by resident FBP blits, streamed FBP rows, and RNG delta writes.
 */

static inline bool
PalFullScreenStretch_SourceCoordinate(
   uint32_t  destination_coordinate,
   uint32_t  destination_extent,
   uint32_t  source_extent,
   uint32_t *source_coordinate
)
{
   uint64_t numerator;

   if (source_coordinate == NULL || source_extent == 0u ||
      destination_extent == 0u ||
      destination_coordinate >= destination_extent)
   {
      return false;
   }
   numerator = ((uint64_t)destination_coordinate * 2u + 1u) *
      source_extent;
   *source_coordinate = (uint32_t)(numerator /
      ((uint64_t)destination_extent * 2u));
   return *source_coordinate < source_extent;
}

static inline uint32_t
PalFullScreenStretch_LowerBound(
   uint32_t source_threshold,
   uint32_t source_extent,
   uint32_t destination_extent
)
{
   uint32_t low = 0u;
   uint32_t high = destination_extent;

   while (low < high)
   {
      uint32_t middle = low + (high - low) / 2u;
      uint32_t mapped = 0u;

      (void)PalFullScreenStretch_SourceCoordinate(middle,
         destination_extent, source_extent, &mapped);
      if (mapped < source_threshold)
      {
         low = middle + 1u;
      }
      else
      {
         high = middle;
      }
   }
   return low;
}

/* Return the half-open destination interval which samples one source pixel. */
static inline bool
PalFullScreenStretch_DestinationRange(
   uint32_t  source_coordinate,
   uint32_t  source_extent,
   uint32_t  destination_extent,
   uint32_t *destination_first,
   uint32_t *destination_after_last
)
{
   uint32_t first;
   uint32_t after_last;

   if (destination_first == NULL || destination_after_last == NULL ||
      source_extent == 0u || destination_extent == 0u ||
      source_coordinate >= source_extent)
   {
      return false;
   }

   if (destination_extent <= source_extent)
   {
      uint32_t candidate = (uint32_t)(
         (((uint64_t)source_coordinate * 2u + 1u) * destination_extent) /
         ((uint64_t)source_extent * 2u));
      uint32_t mapped = 0u;

      if (candidate >= destination_extent ||
         !PalFullScreenStretch_SourceCoordinate(candidate,
            destination_extent, source_extent, &mapped) ||
         mapped != source_coordinate)
      {
         *destination_first = 0u;
         *destination_after_last = 0u;
         return false;
      }
      *destination_first = candidate;
      *destination_after_last = candidate + 1u;
      return true;
   }

   first = PalFullScreenStretch_LowerBound(source_coordinate,
      source_extent, destination_extent);
   after_last = source_coordinate + 1u < source_extent ?
      PalFullScreenStretch_LowerBound(source_coordinate + 1u,
         source_extent, destination_extent) : destination_extent;
   if (first >= after_last)
   {
      *destination_first = 0u;
      *destination_after_last = 0u;
      return false;
   }
   *destination_first = first;
   *destination_after_last = after_last;
   return true;
}

static inline bool
PalFullScreenStretch_BlitIndexedRow(
   const uint8_t *source,
   uint32_t       source_width,
   uint8_t       *destination,
   uint32_t       destination_width
)
{
   uint32_t x;

   if (source == NULL || destination == NULL || source_width == 0u ||
      destination_width == 0u)
   {
      return false;
   }
   for (x = 0u; x < destination_width; x++)
   {
      uint32_t source_x = 0u;

      if (!PalFullScreenStretch_SourceCoordinate(x, destination_width,
         source_width, &source_x))
      {
         return false;
      }
      destination[x] = source[source_x];
   }
   return true;
}

static inline bool
PalFullScreenStretch_BlitIndexed(
   const uint8_t *source,
   uint32_t       source_width,
   uint32_t       source_height,
   size_t         source_pitch,
   uint8_t       *destination,
   uint32_t       destination_width,
   uint32_t       destination_height,
   size_t         destination_pitch
)
{
   uint32_t y;

   if (source == NULL || destination == NULL || source_width == 0u ||
      source_height == 0u || destination_width == 0u ||
      destination_height == 0u || source_pitch < source_width ||
      destination_pitch < destination_width)
   {
      return false;
   }
   for (y = 0u; y < destination_height; y++)
   {
      uint32_t source_y = 0u;

      if (!PalFullScreenStretch_SourceCoordinate(y, destination_height,
         source_height, &source_y) ||
         !PalFullScreenStretch_BlitIndexedRow(
            source + (size_t)source_y * source_pitch,
            source_width,
            destination + (size_t)y * destination_pitch,
            destination_width))
      {
         return false;
      }
   }
   return true;
}

#endif
