#include "pal_engine_pack_provider.h"
#include "pal_memory_profile.h"
#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_HEADER_BYTES 32u
#define TEST_TOC_BYTES 44u
#define TEST_CORE_BYTES 128u
#define TEST_VIRTUAL_PACK_BYTES (64u * 1024u * 1024u)

typedef struct ReadAtContext {
   uint8_t toc[TEST_TOC_BYTES];
   uint32_t calls;
   uint32_t bytes;
} ReadAtContext;

#if defined(MEM_LEVEL2)
uint8_t pal_mem_level2_tf_toc[PAL_MEM_LEVEL2_TF_TOC_BYTES];
uint8_t pal_mem_level2_transient_chunk[
   PAL_MEM_LEVEL2_TRANSIENT_CHUNK_BYTES];
#endif

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

static void
build_toc(
   uint8_t *image,
   uint32_t pack_size,
   uint32_t pack_set_id
)
{
   memset(image, 0, TEST_TOC_BYTES);
   write_le32(image, PAL_PACK_MAGIC);
   write_le16(image + 4u, PAL_PACK_VERSION);
   write_le16(image + 6u, TEST_HEADER_BYTES);
   write_le16(image + 8u, 1u);
   write_le32(image + 12u, TEST_HEADER_BYTES);
   write_le32(image + 16u, TEST_TOC_BYTES);
   write_le32(image + 20u, pack_set_id);
   write_le32(image + 24u, pack_size);
   write_le16(image + TEST_HEADER_BYTES, 1u);
   write_le16(image + TEST_HEADER_BYTES + 2u, 0u);
   write_le32(image + TEST_HEADER_BYTES + 4u, TEST_TOC_BYTES);
}

#if defined(PAL_EXTREME_CHAPTER_CACHE)
static uint32_t
build_core_with_cached_toc(
   uint8_t *image,
   const uint8_t *full_toc,
   uint32_t pack_set_id
)
{
   const uint32_t archive_offset = TEST_HEADER_BYTES;
   const uint32_t chunk_table_offset =
      archive_offset + 12u;
   const uint32_t data_offset = chunk_table_offset + 2u * 16u;
   const uint32_t pack_size = data_offset + TEST_TOC_BYTES;

   memset(image, 0, TEST_CORE_BYTES);
   write_le32(image, PAL_PACK_MAGIC);
   write_le16(image + 4u, PAL_PACK_VERSION);
   write_le16(image + 6u, TEST_HEADER_BYTES);
   write_le16(image + 8u, 1u);
   write_le32(image + 12u, archive_offset);
   write_le32(image + 16u, data_offset);
   write_le32(image + 20u, pack_set_id);
   write_le32(image + 24u, pack_size);

   write_le16(image + archive_offset, PAL_PACK_ARCHIVE_CACHE);
   write_le16(image + archive_offset + 2u, 2u);
   write_le32(image + archive_offset + 4u, chunk_table_offset);
   write_le32(image + chunk_table_offset, data_offset);
   write_le16(image + chunk_table_offset + 8u, PAL_PACK_FORMAT_RAW);
   write_le32(image + chunk_table_offset + 16u, data_offset);
   write_le32(image + chunk_table_offset + 20u, TEST_TOC_BYTES);
   write_le16(image + chunk_table_offset + 24u, PAL_PACK_FORMAT_RAW);
   memcpy(image + data_offset, full_toc, TEST_TOC_BYTES);
   return pack_size;
}
#endif

static bool
read_at(
   void *user,
   uint32_t offset,
   uint8_t *dst,
   uint32_t size
)
{
   ReadAtContext *context = (ReadAtContext *)user;

   if (context == NULL || (dst == NULL && size != 0u) ||
      offset > TEST_TOC_BYTES || size > TEST_TOC_BYTES - offset)
   {
      return false;
   }
   memcpy(dst, context->toc + offset, size);
   context->calls++;
   context->bytes += size;
   return true;
}

static bool
open_pack_set(
   uint32_t pack_set_id
)
{
   uint8_t core[TEST_CORE_BYTES];
   uint32_t core_size;
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   uint8_t overlay[TEST_TOC_BYTES];
#endif
   ReadAtContext full = { { 0u }, 0u, 0u };
   uint32_t active_set_id = 0u;

   build_toc(full.toc, TEST_VIRTUAL_PACK_BYTES, pack_set_id);
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   core_size = build_core_with_cached_toc(
      core, full.toc, pack_set_id);
#else
   core_size = TEST_TOC_BYTES;
   build_toc(core, core_size, pack_set_id);
#endif
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   build_toc(overlay, sizeof(overlay), pack_set_id);
#endif
   PalEngineBridge_ClearPacks();
   if (!PalEngineBridge_SetCorePackConst(core, core_size) ||
      !PalEngineBridge_SetTfPackReadAt(
         TEST_VIRTUAL_PACK_BYTES, read_at, &full) ||
#if defined(PAL_EXTREME_CHAPTER_CACHE)
      !PalEngineBridge_SetOverlayPackConst(overlay, sizeof(overlay)) ||
#endif
      !PalEngineBridge_GetActivePackSetId(&active_set_id) ||
      active_set_id != pack_set_id)
   {
      return false;
   }

   /* Chapter builds compare only the TF header with the NOR-mapped TOC. */
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   return full.calls == 1u && full.bytes == TEST_HEADER_BYTES;
#else
   return full.calls == 2u &&
      full.bytes == TEST_HEADER_BYTES + TEST_TOC_BYTES;
#endif
}

int
main(
   void
)
{
   if (!open_pack_set(0x13579bdfu) ||
      !open_pack_set(0x2468ace1u))
   {
      fprintf(stderr,
         "bounded provider scanned payload data or fixed the data-set ID\n");
      return 2;
   }
   printf("pack_provider_bounded_open_smoke: "
      "arbitrary pack sets opened without payload scans\n");
   return 0;
}
