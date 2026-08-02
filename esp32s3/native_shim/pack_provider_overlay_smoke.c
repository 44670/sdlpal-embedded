#include "pal_engine_pack_provider.h"
#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_ARCHIVE_ID 1u
#define TEST_PACK_CAPACITY 256u
#define TEST_PACK_SET_ID 0x13579bdfu
#define TEST_HEADER_SIZE 32u
#define TEST_ARCHIVE_ENTRY_SIZE 12u
#define TEST_CHUNK_ENTRY_SIZE 16u
#define TEST_MAX_CHUNKS 5u

typedef struct ReadAtContext {
   const uint8_t *image;
   uint32_t size;
   uint32_t calls;
} ReadAtContext;

FILE *__wrap_PAL_MKFOpenPackArchive(unsigned int archive_id);
int __wrap_PAL_MKFGetChunkCount(FILE *fp);
int __wrap_PAL_MKFGetChunkSize(unsigned int chunk_id, FILE *fp);
int __wrap_PAL_MKFReadChunk(
   uint8_t *buffer,
   unsigned int buffer_size,
   unsigned int chunk_id,
   FILE *fp);

static void
write_le16(
   uint8_t *dst,
   uint16_t value
)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
}

static void
write_le32(
   uint8_t *dst,
   uint32_t value
)
{
   dst[0] = (uint8_t)value;
   dst[1] = (uint8_t)(value >> 8);
   dst[2] = (uint8_t)(value >> 16);
   dst[3] = (uint8_t)(value >> 24);
}

static uint32_t
pack_crc32(
   const uint8_t *image,
   uint32_t       size
)
{
   static const uint32_t nibble_table[16] = {
      0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
      0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
      0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
      0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
   };
   uint32_t crc = 0xffffffffu;
   uint32_t i;

   for (i = 0; i < size; i++)
   {
      crc ^= image[i];
      crc = (crc >> 4) ^ nibble_table[crc & 0x0fu];
      crc = (crc >> 4) ^ nibble_table[crc & 0x0fu];
   }
   return crc ^ 0xffffffffu;
}

static uint32_t
build_pack(
   uint8_t *image,
   uint16_t chunk_count,
   uint32_t pack_set_id,
   const uint8_t *const chunks[TEST_MAX_CHUNKS],
   const uint32_t sizes[TEST_MAX_CHUNKS]
)
{
   uint32_t archive_offset = TEST_HEADER_SIZE;
   uint32_t chunk_table_offset = archive_offset + TEST_ARCHIVE_ENTRY_SIZE;
   uint32_t data_offset = chunk_table_offset +
      (uint32_t)chunk_count * TEST_CHUNK_ENTRY_SIZE;
   uint32_t payload_offset = data_offset;
   uint16_t chunk_id;

   memset(image, 0, TEST_PACK_CAPACITY);
   for (chunk_id = 0; chunk_id < chunk_count; chunk_id++)
   {
      uint8_t *entry = image + chunk_table_offset +
         (uint32_t)chunk_id * TEST_CHUNK_ENTRY_SIZE;

      if (payload_offset > TEST_PACK_CAPACITY ||
         sizes[chunk_id] > TEST_PACK_CAPACITY - payload_offset)
      {
         return 0;
      }
      write_le32(entry, payload_offset);
      write_le32(entry + 4, sizes[chunk_id]);
      write_le16(entry + 8, PAL_PACK_FORMAT_NATIVE);
      if (sizes[chunk_id] != 0)
      {
         memcpy(image + payload_offset, chunks[chunk_id], sizes[chunk_id]);
         payload_offset += sizes[chunk_id];
      }
   }

   write_le32(image, PAL_PACK_MAGIC);
   write_le16(image + 4, PAL_PACK_VERSION);
   write_le16(image + 6, TEST_HEADER_SIZE);
   write_le16(image + 8, 1);
   write_le32(image + 12, archive_offset);
   write_le32(image + 16, data_offset);
   write_le32(image + 20, pack_set_id);
   write_le32(image + 24, payload_offset);

   write_le16(image + archive_offset, TEST_ARCHIVE_ID);
   write_le16(image + archive_offset + 2, chunk_count);
   write_le32(image + archive_offset + 4, chunk_table_offset);
   write_le32(image + 28, pack_crc32(image, payload_offset));
   return payload_offset;
}

static bool
read_at(
   void *user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size
)
{
   ReadAtContext *ctx = (ReadAtContext *)user;

   if (ctx == NULL || (dst == NULL && size != 0) ||
      offset > ctx->size || size > ctx->size - offset)
   {
      return false;
   }
   if (size != 0)
   {
      memcpy(dst, ctx->image + offset, size);
   }
   ctx->calls++;
   return true;
}

static bool
expect_chunk(
   FILE *archive,
   uint16_t chunk_id,
   const char *expected
)
{
   uint8_t buffer[32];
   size_t expected_size = strlen(expected);
   int result;

   memset(buffer, 0, sizeof(buffer));
   if (__wrap_PAL_MKFGetChunkSize(chunk_id, archive) !=
         (int)expected_size)
   {
      return false;
   }
   result = __wrap_PAL_MKFReadChunk(buffer, sizeof(buffer),
      chunk_id, archive);
   return result == (int)expected_size &&
      memcmp(buffer, expected, expected_size) == 0;
}

int
main(
   void
)
{
   static uint8_t core_image[TEST_PACK_CAPACITY];
   static uint8_t core_mismatch_image[TEST_PACK_CAPACITY];
   static uint8_t overlay_image[TEST_PACK_CAPACITY];
   static uint8_t overlay_replacement_image[TEST_PACK_CAPACITY];
   static uint8_t overlay_core_duplicate_image[TEST_PACK_CAPACITY];
   static uint8_t overlay_tf_duplicate_image[TEST_PACK_CAPACITY];
   static uint8_t tf_image[TEST_PACK_CAPACITY];
   static uint8_t tf_core_duplicate_image[TEST_PACK_CAPACITY];
   static const uint8_t core0[] = "core0";
   static const uint8_t overlay1[] = "overlay1";
   static const uint8_t overlay1b[] = "overlay1b";
   static const uint8_t overlay3[] = "overlay3";
   static const uint8_t tf2[] = "tf2";
   static const uint8_t tf4[] = "tf4";
   const uint8_t *core_chunks[TEST_MAX_CHUNKS] = {
      core0, NULL, NULL, NULL, NULL
   };
   const uint32_t core_sizes[TEST_MAX_CHUNKS] = {
      sizeof(core0) - 1u, 0, 0, 0, 0
   };
   const uint8_t *overlay_chunks[TEST_MAX_CHUNKS] = {
      NULL, overlay1, NULL, overlay3, NULL
   };
   const uint32_t overlay_sizes[TEST_MAX_CHUNKS] = {
      0, sizeof(overlay1) - 1u, 0, sizeof(overlay3) - 1u, 0
   };
   const uint8_t *overlay_replacement_chunks[TEST_MAX_CHUNKS] = {
      NULL, overlay1b, NULL, overlay3, NULL
   };
   const uint32_t overlay_replacement_sizes[TEST_MAX_CHUNKS] = {
      0, sizeof(overlay1b) - 1u, 0, sizeof(overlay3) - 1u, 0
   };
   const uint8_t *overlay_core_duplicate_chunks[TEST_MAX_CHUNKS] = {
      core0, NULL, NULL, NULL, NULL
   };
   const uint8_t *overlay_tf_duplicate_chunks[TEST_MAX_CHUNKS] = {
      NULL, NULL, tf2, NULL, NULL
   };
   const uint32_t duplicate_sizes[TEST_MAX_CHUNKS] = {
      sizeof(core0) - 1u, 0, 0, 0, 0
   };
   const uint32_t tf_duplicate_sizes[TEST_MAX_CHUNKS] = {
      0, 0, sizeof(tf2) - 1u, 0, 0
   };
   const uint8_t *tf_chunks[TEST_MAX_CHUNKS] = {
      NULL, NULL, tf2, NULL, tf4
   };
   const uint32_t tf_sizes[TEST_MAX_CHUNKS] = {
      0, 0, sizeof(tf2) - 1u, 0, sizeof(tf4) - 1u
   };
   uint32_t core_size;
   uint32_t core_mismatch_size;
   uint32_t overlay_size;
   uint32_t overlay_replacement_size;
   uint32_t overlay_core_duplicate_size;
   uint32_t overlay_tf_duplicate_size;
   uint32_t tf_size;
   uint32_t tf_core_duplicate_size;
   ReadAtContext tf;
   ReadAtContext tf_core_duplicate;
   FILE *archive;

   core_size = build_pack(core_image, 3, TEST_PACK_SET_ID,
      core_chunks, core_sizes);
   core_mismatch_size = build_pack(core_mismatch_image, 3,
      TEST_PACK_SET_ID + 1u, core_chunks, core_sizes);
   overlay_size = build_pack(overlay_image, 4, TEST_PACK_SET_ID,
      overlay_chunks, overlay_sizes);
   overlay_replacement_size = build_pack(overlay_replacement_image, 4,
      TEST_PACK_SET_ID, overlay_replacement_chunks,
      overlay_replacement_sizes);
   overlay_core_duplicate_size = build_pack(
      overlay_core_duplicate_image, 5, TEST_PACK_SET_ID,
      overlay_core_duplicate_chunks, duplicate_sizes);
   overlay_tf_duplicate_size = build_pack(
      overlay_tf_duplicate_image, 5, TEST_PACK_SET_ID,
      overlay_tf_duplicate_chunks, tf_duplicate_sizes);
   tf_size = build_pack(tf_image, 5, TEST_PACK_SET_ID,
      tf_chunks, tf_sizes);
   tf_core_duplicate_size = build_pack(tf_core_duplicate_image, 5,
      TEST_PACK_SET_ID, overlay_core_duplicate_chunks, duplicate_sizes);
   if (core_size == 0 || core_mismatch_size == 0 ||
      overlay_size == 0 || overlay_replacement_size == 0 ||
      overlay_core_duplicate_size == 0 ||
      overlay_tf_duplicate_size == 0 || tf_size == 0 ||
      tf_core_duplicate_size == 0)
   {
      fprintf(stderr, "synthetic pack construction failed\n");
      return 2;
   }

   tf.image = tf_image;
   tf.size = tf_size;
   tf.calls = 0;
   tf_core_duplicate.image = tf_core_duplicate_image;
   tf_core_duplicate.size = tf_core_duplicate_size;
   tf_core_duplicate.calls = 0;

   PalEngineBridge_ClearPacks();
   if (PalEngineBridge_SetOverlayPackConst(overlay_image, overlay_size) ||
      !PalEngineBridge_SetNorPackConst(core_image, core_size) ||
      !PalEngineBridge_SetTfPackReadAt(tf_size, read_at, &tf) ||
      !PalEngineBridge_SetOverlayPackConst(overlay_image, overlay_size))
   {
      fprintf(stderr, "core/overlay/TF setup failed\n");
      return 2;
   }

   archive = __wrap_PAL_MKFOpenPackArchive(TEST_ARCHIVE_ID);
   if (archive == NULL || __wrap_PAL_MKFGetChunkCount(archive) != 5 ||
      !expect_chunk(archive, 0, "core0") ||
      !expect_chunk(archive, 1, "overlay1") ||
      !expect_chunk(archive, 2, "tf2") ||
      !expect_chunk(archive, 3, "overlay3") ||
      !expect_chunk(archive, 4, "tf4"))
   {
      fprintf(stderr, "overlay/core/TF lookup order failed\n");
      return 2;
   }

   if (PalEngineBridge_SetNorPackConst(core_mismatch_image,
         core_mismatch_size) ||
      PalEngineBridge_SetOverlayPackConst(overlay_core_duplicate_image,
         overlay_core_duplicate_size) ||
      PalEngineBridge_SetOverlayPackConst(overlay_tf_duplicate_image,
         overlay_tf_duplicate_size) ||
      !expect_chunk(archive, 0, "core0") ||
      !expect_chunk(archive, 1, "overlay1"))
   {
      fprintf(stderr, "set-id or duplicate rejection failed\n");
      return 2;
   }

   if (PalEngineBridge_SetTfPackReadAt(tf_core_duplicate_size,
         read_at, &tf_core_duplicate) ||
      !expect_chunk(archive, 0, "core0") ||
      !expect_chunk(archive, 1, "overlay1") ||
      !PalEngineBridge_SetTfPackReadAt(tf_size, read_at, &tf))
   {
      fprintf(stderr, "duplicate TF rejection or recovery failed\n");
      return 2;
   }

   if (!PalEngineBridge_SetOverlayPackConst(overlay_replacement_image,
         overlay_replacement_size) ||
      !expect_chunk(archive, 1, "overlay1b"))
   {
      fprintf(stderr, "overlay replacement failed\n");
      return 2;
   }

   PalEngineBridge_ClearOverlay();
   if (__wrap_PAL_MKFGetChunkCount(archive) != 5 ||
      __wrap_PAL_MKFGetChunkSize(1, archive) != 0 ||
      !expect_chunk(archive, 0, "core0") ||
      !expect_chunk(archive, 2, "tf2") ||
      !PalEngineBridge_SetOverlayPackConst(overlay_image, overlay_size) ||
      !expect_chunk(archive, 1, "overlay1"))
   {
      fprintf(stderr, "ClearOverlay affected core or TF\n");
      return 2;
   }

   PalEngineBridge_ClearPacks();
   if (__wrap_PAL_MKFOpenPackArchive(TEST_ARCHIVE_ID) != NULL)
   {
      fprintf(stderr, "ClearPacks left a pack layer active\n");
      return 2;
   }

   printf("pack_provider_overlay_smoke: core/overlay/TF layering passed\n");
   return 0;
}
