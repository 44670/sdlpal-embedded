#include "../engine_bridge/pal_engine_chapter_cache.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void
write_le16(
   uint8_t *p,
   uint16_t value
)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
}

static void
write_le32(
   uint8_t *p,
   uint32_t value
)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
   p[2] = (uint8_t)(value >> 16);
   p[3] = (uint8_t)(value >> 24);
}

int
main(
   void
)
{
   static const uint8_t abc_sha256[32] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
      0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
      0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
      0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
   };
   uint8_t catalog_image[
      PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET +
      PAL_ENGINE_CACHE_DESCRIPTOR_BYTES];
   uint8_t set_image[
      PAL_ENGINE_CACHE_SET_HEADER_BYTES + sizeof(catalog_image)];
   uint8_t commit[PAL_ENGINE_CACHE_COMMIT_BYTES];
   uint8_t partial_commit[PAL_ENGINE_CACHE_COMMIT_BYTES];
   uint8_t digest[32];
   PalEngineChapterCatalog catalog;
   PalEngineChapterSet set;
   PalEngineChapterDescriptor descriptor;
   PalEngineChapterDescriptor actual;
   uint32_t generation;
   uint32_t crc;
   unsigned i;

   PalEngineChapterCache_Sha256((const uint8_t *)"abc", 3u, digest);
   assert(memcmp(digest, abc_sha256, sizeof(digest)) == 0);

   memset(catalog_image, 0, sizeof(catalog_image));
   write_le32(catalog_image, PAL_ENGINE_CACHE_CATALOG_MAGIC);
   write_le16(catalog_image + 4u, PAL_ENGINE_CACHE_CATALOG_VERSION);
   write_le16(catalog_image + 6u, PAL_ENGINE_CACHE_CATALOG_HEADER_BYTES);
   write_le32(catalog_image + 8u, 0x12345678u);
   write_le16(catalog_image + 12u, PAL_ENGINE_CACHE_CATALOG_SCENES);
   write_le16(catalog_image + 14u, 1u);
   write_le32(catalog_image + 16u,
      PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET);
   write_le32(catalog_image + 20u,
      PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET);
   write_le32(catalog_image + 24u, sizeof(catalog_image));
   catalog_image[PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET] = UINT8_MAX;
   for (i = 1u; i < PAL_ENGINE_CACHE_CATALOG_SCENES; i++)
   {
      catalog_image[PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET + i] = 7u;
   }
   catalog_image[PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET] = 7u;
   write_le32(catalog_image +
      PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET + 4u, 1234u);
   memcpy(catalog_image +
      PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET + 8u,
      abc_sha256, sizeof(abc_sha256));
   crc = PalEngineChapterCache_Crc32(catalog_image,
      sizeof(catalog_image), 28u, 4u);
   write_le32(catalog_image + 28u, crc);

   assert(PalEngineChapterCache_OpenCatalog(&catalog,
      catalog_image, sizeof(catalog_image)));
   assert(!PalEngineChapterCache_DescribeScene(&catalog, 0u, &actual));
   assert(PalEngineChapterCache_DescribeScene(&catalog, 1u, &actual));
   assert(PalEngineChapterCache_DescribeScene(&catalog, 299u, &actual));
   assert(!PalEngineChapterCache_DescribeScene(&catalog, 300u, &actual));
   assert(actual.bundle_id == 7u);
   assert(actual.pack_size == 1234u);
   assert(memcmp(actual.sha256, abc_sha256, sizeof(actual.sha256)) == 0);

   memset(set_image, 0, sizeof(set_image));
   write_le32(set_image, PAL_ENGINE_CACHE_SET_MAGIC);
   write_le16(set_image + 4u, PAL_ENGINE_CACHE_SET_VERSION);
   write_le16(set_image + 6u, PAL_ENGINE_CACHE_SET_HEADER_BYTES);
   write_le32(set_image + 8u, sizeof(set_image));
   write_le32(set_image + 12u, catalog.set_id);
   write_le32(set_image + 16u, 4u);
   write_le32(set_image + 20u, PAL_ENGINE_CACHE_SET_HEADER_BYTES);
   write_le32(set_image + 24u, sizeof(catalog_image));
   PalEngineChapterCache_Sha256((const uint8_t *)"core", 4u,
      set_image + PAL_ENGINE_CACHE_SET_CORE_SHA256_OFFSET);
   memcpy(set_image + PAL_ENGINE_CACHE_SET_HEADER_BYTES,
      catalog_image, sizeof(catalog_image));
   crc = PalEngineChapterCache_Crc32(set_image, sizeof(set_image),
      PAL_ENGINE_CACHE_SET_CRC32_OFFSET, 4u);
   write_le32(set_image + PAL_ENGINE_CACHE_SET_CRC32_OFFSET, crc);
   assert(PalEngineChapterCache_OpenSet(&set,
      set_image, sizeof(set_image)));
   assert(set.set_id == catalog.set_id);
   assert(set.core_size == 4u);
   assert(set.catalog.image_size == sizeof(catalog_image));
   assert(memcmp(set.catalog.image, catalog_image,
      sizeof(catalog_image)) == 0);
   set_image[PAL_ENGINE_CACHE_SET_HEADER_BYTES] ^= 1u;
   assert(!PalEngineChapterCache_OpenSet(&set,
      set_image, sizeof(set_image)));
   set_image[PAL_ENGINE_CACHE_SET_HEADER_BYTES] ^= 1u;

   descriptor = actual;
   assert(PalEngineChapterCache_BuildCommit(commit, sizeof(commit),
      catalog.set_id, &descriptor, 19u, catalog.crc32));
   assert(PalEngineChapterCache_Decide(commit, sizeof(commit),
      catalog.set_id, &descriptor, catalog.crc32, &generation) ==
      PAL_ENGINE_CHAPTER_CACHE_VERIFY_PAYLOAD);
   assert(generation == 19u);
   memcpy(partial_commit, commit, sizeof(partial_commit));
   memset(partial_commit + 8u, 0xff, 4u);
   assert(PalEngineChapterCache_Decide(
      partial_commit, sizeof(partial_commit),
      catalog.set_id, &descriptor, catalog.crc32, NULL) ==
      PAL_ENGINE_CHAPTER_CACHE_REBUILD);
   for (i = 0; i < sizeof(commit); i++)
   {
      memset(partial_commit, 0xff, sizeof(partial_commit));
      memcpy(partial_commit, commit, i);
      assert(PalEngineChapterCache_Decide(
         partial_commit, sizeof(partial_commit),
         catalog.set_id, &descriptor, catalog.crc32, NULL) ==
         PAL_ENGINE_CHAPTER_CACHE_REBUILD);
   }
   commit[24] ^= 1u;
   assert(PalEngineChapterCache_Decide(commit, sizeof(commit),
      catalog.set_id, &descriptor, catalog.crc32, &generation) ==
      PAL_ENGINE_CHAPTER_CACHE_REBUILD);

   descriptor.pack_size = 3u;
   memcpy(descriptor.sha256, abc_sha256, sizeof(descriptor.sha256));
   assert(PalEngineChapterCache_PayloadMatches(
      &descriptor, (const uint8_t *)"abc", 3u));
   assert(!PalEngineChapterCache_PayloadMatches(
      &descriptor, (const uint8_t *)"abd", 3u));
   assert(!PalEngineChapterCache_PayloadMatches(
      &descriptor, (const uint8_t *)"abc", 2u));

   catalog_image[PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET] = 0u;
   assert(!PalEngineChapterCache_OpenCatalog(&catalog,
      catalog_image, sizeof(catalog_image)));

   puts("chapter cache format/decision: ok");
   return 0;
}
