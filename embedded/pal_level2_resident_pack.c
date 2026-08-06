#include "pal_level2_resident_pack.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

enum {
   PAL_LEVEL2_PACK_HEADER_BYTES = 32u,
   PAL_LEVEL2_ARCHIVE_ENTRY_BYTES = 12u,
   PAL_LEVEL2_CHUNK_ENTRY_BYTES = 16u,
};

static const uint16_t pal_level2_resident_archives[] = {
   PAL_PACK_ARCHIVE_DATA,
   PAL_PACK_ARCHIVE_MUS,
   PAL_PACK_ARCHIVE_PAT,
   PAL_PACK_ARCHIVE_RGM,
   PAL_PACK_ARCHIVE_SSS,
   PAL_PACK_ARCHIVE_TEXT,
   PAL_PACK_ARCHIVE_FONT,
};

static void
write_le16(
   uint8_t *p,
   uint16_t value)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
}

static void
write_le32(
   uint8_t *p,
   uint32_t value)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
   p[2] = (uint8_t)(value >> 16);
   p[3] = (uint8_t)(value >> 24);
}

static uint32_t
read_le32(
   const uint8_t *p)
{
   return (uint32_t)p[0] |
      ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) |
      ((uint32_t)p[3] << 24);
}

static uint32_t
align4(
   uint32_t value)
{
   return (value + 3u) & ~3u;
}

static bool
copy_range(
   PalLevel2ResidentReadAt read_at,
   void *read_user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size)
{
   while (size != 0u)
   {
      uint32_t amount = size > 32768u ? 32768u : size;

      if (!read_at(read_user, offset, dst, amount))
      {
         return false;
      }
      offset += amount;
      dst += amount;
      size -= amount;
   }
   return true;
}

static bool
resident_chunk_selected(
   uint16_t archive_id,
   uint16_t chunk_id)
{
   /* Native FONT10 supersedes the legacy DOS font in chunk zero. */
   return archive_id != PAL_PACK_ARCHIVE_FONT || chunk_id == 1u;
}

bool
PalLevel2ResidentPack_Build(
   const PalPackToc *full_toc,
   uint32_t archive_mask,
   PalLevel2ResidentReadAt read_at,
   void *read_user,
   uint8_t *image,
   uint32_t image_capacity,
   uint32_t *out_size)
{
   uint16_t selected_archives[
      sizeof(pal_level2_resident_archives) /
      sizeof(pal_level2_resident_archives[0])];
   uint16_t chunk_counts[
      sizeof(selected_archives) / sizeof(selected_archives[0])];
   uint16_t archive_count = 0u;
   uint32_t archive_table = PAL_LEVEL2_PACK_HEADER_BYTES;
   uint32_t chunk_table = archive_table +
      (uint32_t)archive_count * PAL_LEVEL2_ARCHIVE_ENTRY_BYTES;
   uint32_t data_offset;
   uint32_t cursor;
   uint16_t archive_index;

   if (full_toc == NULL || full_toc->base == NULL || archive_mask == 0u ||
      (archive_mask & ~PAL_LEVEL2_RESIDENT_DEFAULT_MASK) != 0u ||
      read_at == NULL ||
      image == NULL || out_size == NULL ||
      image_capacity < PAL_LEVEL2_PACK_HEADER_BYTES)
   {
      return false;
   }

   for (archive_index = 0u;
      archive_index < sizeof(pal_level2_resident_archives) /
         sizeof(pal_level2_resident_archives[0]);
      archive_index++)
   {
      uint16_t archive_id = pal_level2_resident_archives[archive_index];

      if ((archive_mask & PAL_LEVEL2_RESIDENT_ARCHIVE_BIT(archive_id)) != 0u)
      {
         selected_archives[archive_count++] = archive_id;
      }
   }
   archive_table = PAL_LEVEL2_PACK_HEADER_BYTES;
   chunk_table = archive_table +
      (uint32_t)archive_count * PAL_LEVEL2_ARCHIVE_ENTRY_BYTES;

   for (archive_index = 0u; archive_index < archive_count; archive_index++)
   {
      if (!PalPackToc_GetChunkCount(full_toc,
            selected_archives[archive_index],
            &chunk_counts[archive_index]) ||
         chunk_counts[archive_index] >
            (image_capacity - chunk_table) /
            PAL_LEVEL2_CHUNK_ENTRY_BYTES)
      {
         return false;
      }
      chunk_table += (uint32_t)chunk_counts[archive_index] *
         PAL_LEVEL2_CHUNK_ENTRY_BYTES;
   }
   data_offset = align4(chunk_table);
   if (data_offset > image_capacity)
   {
      return false;
   }
   memset(image, 0, data_offset);
   chunk_table = archive_table +
      (uint32_t)archive_count * PAL_LEVEL2_ARCHIVE_ENTRY_BYTES;
   cursor = data_offset;

   for (archive_index = 0u; archive_index < archive_count; archive_index++)
   {
      uint16_t archive_id = selected_archives[archive_index];
      uint16_t chunk_count = chunk_counts[archive_index];
      uint8_t *archive_entry = image + archive_table +
         (uint32_t)archive_index * PAL_LEVEL2_ARCHIVE_ENTRY_BYTES;
      uint32_t source_first = 0u;
      uint32_t source_end = 0u;
      uint32_t destination_first;
      uint16_t chunk_id;

      write_le16(archive_entry, archive_id);
      write_le16(archive_entry + 2u, chunk_count);
      write_le32(archive_entry + 4u, chunk_table);
      for (chunk_id = 0u; chunk_id < chunk_count; chunk_id++)
      {
         PalPackChunkInfo info;

         if (!PalPackToc_GetChunkInfo(
               full_toc, archive_id, chunk_id, &info) ||
            info.flags != 0u)
         {
            return false;
         }
         if (info.size != 0u &&
            resident_chunk_selected(archive_id, chunk_id))
         {
            if (source_first == 0u)
            {
               source_first = info.offset;
            }
            else if (info.offset != align4(source_end))
            {
               return false;
            }
            if (info.offset > UINT32_MAX - info.size)
            {
               return false;
            }
            source_end = info.offset + info.size;
         }
      }
      destination_first = align4(cursor);
      if (source_first != 0u)
      {
         uint32_t span = source_end - source_first;

         if (destination_first > image_capacity ||
            span > image_capacity - destination_first ||
            !copy_range(read_at, read_user, source_first,
               image + destination_first, span))
         {
            return false;
         }
         cursor = destination_first + span;
      }
      for (chunk_id = 0u; chunk_id < chunk_count; chunk_id++)
      {
         PalPackChunkInfo info;
         uint8_t *chunk_entry = image + chunk_table +
            (uint32_t)chunk_id * PAL_LEVEL2_CHUNK_ENTRY_BYTES;
         uint32_t destination = destination_first;

         if (!PalPackToc_GetChunkInfo(
               full_toc, archive_id, chunk_id, &info))
         {
            return false;
         }
         if (!resident_chunk_selected(archive_id, chunk_id))
         {
            info.size = 0u;
         }
         if (info.size != 0u)
         {
            destination += info.offset - source_first;
         }
         write_le32(chunk_entry, destination);
         write_le32(chunk_entry + 4u, info.size);
         write_le16(chunk_entry + 8u, info.format);
         write_le16(chunk_entry + 10u, info.flags);
      }
      chunk_table +=
         (uint32_t)chunk_count * PAL_LEVEL2_CHUNK_ENTRY_BYTES;
   }

   write_le32(image, PAL_PACK_MAGIC);
   write_le16(image + 4u, PAL_PACK_VERSION);
   write_le16(image + 6u, PAL_LEVEL2_PACK_HEADER_BYTES);
   write_le16(image + 8u, archive_count);
   write_le32(image + 12u, archive_table);
   write_le32(image + 16u, data_offset);
   write_le32(image + 20u, read_le32(full_toc->base + 20u));
   write_le32(image + 24u, cursor);
   *out_size = cursor;
   return true;
}
