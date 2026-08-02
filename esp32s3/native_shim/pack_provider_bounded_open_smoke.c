#include "pal_engine_pack_provider.h"
#include "pal_memory_profile.h"
#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_HEADER_BYTES 32u
#define TEST_TOC_BYTES 44u
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
   uint8_t core[TEST_TOC_BYTES];
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   uint8_t overlay[TEST_TOC_BYTES];
#endif
   ReadAtContext full = { { 0u }, 0u, 0u };
   uint32_t active_set_id = 0u;

   build_toc(core, sizeof(core), pack_set_id);
#if defined(PAL_EXTREME_CHAPTER_CACHE)
   build_toc(overlay, sizeof(overlay), pack_set_id);
#endif
   build_toc(full.toc, TEST_VIRTUAL_PACK_BYTES, pack_set_id);
   PalEngineBridge_ClearPacks();
   if (!PalEngineBridge_SetCorePackConst(core, sizeof(core)) ||
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

   /* OpenTocRead reads the 32-byte header, then the bounded TOC only. */
   return full.calls == 2u &&
      full.bytes == TEST_HEADER_BYTES + TEST_TOC_BYTES;
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
      "arbitrary pack sets opened from bounded TOCs\n");
   return 0;
}
