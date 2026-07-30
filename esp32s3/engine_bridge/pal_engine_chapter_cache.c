#include "pal_engine_chapter_cache.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PAL_CATALOG_SET_ID_OFFSET 8u
#define PAL_CATALOG_SCENE_COUNT_OFFSET 12u
#define PAL_CATALOG_BUNDLE_COUNT_OFFSET 14u
#define PAL_CATALOG_SCENE_TABLE_OFFSET 16u
#define PAL_CATALOG_DESCRIPTOR_TABLE_OFFSET 20u
#define PAL_CATALOG_TOTAL_BYTES_OFFSET 24u
#define PAL_CATALOG_CRC32_OFFSET 28u

#define PAL_DESCRIPTOR_ID_OFFSET 0u
#define PAL_DESCRIPTOR_SIZE_OFFSET 4u
#define PAL_DESCRIPTOR_SHA256_OFFSET 8u

#define PAL_COMMIT_STATE_OFFSET 8u
#define PAL_COMMIT_SET_ID_OFFSET 12u
#define PAL_COMMIT_BUNDLE_ID_OFFSET 16u
#define PAL_COMMIT_PAYLOAD_SIZE_OFFSET 20u
#define PAL_COMMIT_SHA256_OFFSET 24u
#define PAL_COMMIT_CATALOG_CRC32_OFFSET 56u
#define PAL_COMMIT_GENERATION_OFFSET 60u
#define PAL_COMMIT_CRC32_OFFSET 76u

typedef struct PalChapterSha256 {
   uint32_t state[8];
   uint32_t byte_count_low;
   uint32_t byte_count_high;
   uint32_t buffered;
   uint8_t block[64];
} PalChapterSha256;

static uint16_t
read_le16(
   const uint8_t *p
)
{
   return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t
read_le32(
   const uint8_t *p
)
{
   return (uint32_t)p[0] |
      ((uint32_t)p[1] << 8) |
      ((uint32_t)p[2] << 16) |
      ((uint32_t)p[3] << 24);
}

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

static bool
checked_range(
   uint32_t offset,
   uint32_t length,
   uint32_t size
)
{
   return offset <= size && length <= size - offset;
}

uint32_t
PalEngineChapterCache_Crc32(
   const uint8_t *bytes,
   uint32_t size,
   uint32_t zero_offset,
   uint32_t zero_size
)
{
   uint32_t crc = 0xffffffffu;
   uint32_t i;

   if (bytes == NULL && size != 0u)
   {
      return 0u;
   }
   for (i = 0; i < size; i++)
   {
      uint32_t value;
      unsigned bit;
      uint8_t byte = (i >= zero_offset && i - zero_offset < zero_size)
         ? 0u : bytes[i];

      value = crc ^ byte;
      for (bit = 0; bit < 8u; bit++)
      {
         value = (value >> 1) ^
            (0xedb88320u & (uint32_t)-(int32_t)(value & 1u));
      }
      crc = value;
   }
   return crc ^ 0xffffffffu;
}

static uint32_t
rotate_right(
   uint32_t value,
   unsigned bits
)
{
   return (value >> bits) | (value << (32u - bits));
}

static void
sha256_transform(
   PalChapterSha256 *ctx,
   const uint8_t block[64]
)
{
   static const uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
      0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
      0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
      0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
      0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
   };
   uint32_t w[64];
   uint32_t a;
   uint32_t b;
   uint32_t c;
   uint32_t d;
   uint32_t e;
   uint32_t f;
   uint32_t g;
   uint32_t h;
   unsigned i;

   for (i = 0; i < 16u; i++)
   {
      const uint8_t *p = block + i * 4u;
      w[i] = ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
   }
   for (; i < 64u; i++)
   {
      uint32_t s0 = rotate_right(w[i - 15u], 7u) ^
         rotate_right(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
      uint32_t s1 = rotate_right(w[i - 2u], 17u) ^
         rotate_right(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
      w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
   }

   a = ctx->state[0];
   b = ctx->state[1];
   c = ctx->state[2];
   d = ctx->state[3];
   e = ctx->state[4];
   f = ctx->state[5];
   g = ctx->state[6];
   h = ctx->state[7];
   for (i = 0; i < 64u; i++)
   {
      uint32_t sum1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^
         rotate_right(e, 25u);
      uint32_t choose = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + sum1 + choose + k[i] + w[i];
      uint32_t sum0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^
         rotate_right(a, 22u);
      uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = sum0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
   }
   ctx->state[0] += a;
   ctx->state[1] += b;
   ctx->state[2] += c;
   ctx->state[3] += d;
   ctx->state[4] += e;
   ctx->state[5] += f;
   ctx->state[6] += g;
   ctx->state[7] += h;
}

static void
sha256_init(
   PalChapterSha256 *ctx
)
{
   memset(ctx, 0, sizeof(*ctx));
   ctx->state[0] = 0x6a09e667u;
   ctx->state[1] = 0xbb67ae85u;
   ctx->state[2] = 0x3c6ef372u;
   ctx->state[3] = 0xa54ff53au;
   ctx->state[4] = 0x510e527fu;
   ctx->state[5] = 0x9b05688cu;
   ctx->state[6] = 0x1f83d9abu;
   ctx->state[7] = 0x5be0cd19u;
}

static void
sha256_update(
   PalChapterSha256 *ctx,
   const uint8_t *bytes,
   uint32_t size
)
{
   uint32_t old_low = ctx->byte_count_low;

   ctx->byte_count_low += size;
   if (ctx->byte_count_low < old_low)
   {
      ctx->byte_count_high++;
   }

   while (size != 0u)
   {
      uint32_t amount = 64u - ctx->buffered;
      if (amount > size)
      {
         amount = size;
      }
      memcpy(ctx->block + ctx->buffered, bytes, amount);
      ctx->buffered += amount;
      bytes += amount;
      size -= amount;
      if (ctx->buffered == 64u)
      {
         sha256_transform(ctx, ctx->block);
         ctx->buffered = 0u;
      }
   }
}

static void
sha256_finish(
   PalChapterSha256 *ctx,
   uint8_t digest[32]
)
{
   uint32_t bit_low = ctx->byte_count_low << 3;
   uint32_t bit_high = (ctx->byte_count_high << 3) |
      (ctx->byte_count_low >> 29);
   unsigned i;

   ctx->block[ctx->buffered++] = 0x80u;
   if (ctx->buffered > 56u)
   {
      memset(ctx->block + ctx->buffered, 0, 64u - ctx->buffered);
      sha256_transform(ctx, ctx->block);
      ctx->buffered = 0u;
   }
   memset(ctx->block + ctx->buffered, 0, 56u - ctx->buffered);
   ctx->block[56] = (uint8_t)(bit_high >> 24);
   ctx->block[57] = (uint8_t)(bit_high >> 16);
   ctx->block[58] = (uint8_t)(bit_high >> 8);
   ctx->block[59] = (uint8_t)bit_high;
   ctx->block[60] = (uint8_t)(bit_low >> 24);
   ctx->block[61] = (uint8_t)(bit_low >> 16);
   ctx->block[62] = (uint8_t)(bit_low >> 8);
   ctx->block[63] = (uint8_t)bit_low;
   sha256_transform(ctx, ctx->block);

   for (i = 0; i < 8u; i++)
   {
      digest[i * 4u] = (uint8_t)(ctx->state[i] >> 24);
      digest[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
      digest[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
      digest[i * 4u + 3u] = (uint8_t)ctx->state[i];
   }
}

void
PalEngineChapterCache_Sha256(
   const uint8_t *bytes,
   uint32_t size,
   uint8_t digest[32]
)
{
   PalChapterSha256 ctx;

   if (digest == NULL || (bytes == NULL && size != 0u))
   {
      return;
   }
   sha256_init(&ctx);
   sha256_update(&ctx, bytes, size);
   sha256_finish(&ctx, digest);
}

static const uint8_t *
catalog_descriptor_at(
   const PalEngineChapterCatalog *catalog,
   uint16_t index
)
{
   return catalog->image + PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET +
      (uint32_t)index * PAL_ENGINE_CACHE_DESCRIPTOR_BYTES;
}

bool
PalEngineChapterCache_OpenCatalog(
   PalEngineChapterCatalog *catalog,
   const uint8_t *image,
   uint32_t image_size
)
{
   uint16_t bundle_count;
   uint32_t expected_total;
   uint32_t declared_crc;
   uint16_t i;

   if (catalog == NULL || image == NULL ||
      image_size < PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET ||
      read_le32(image) != PAL_ENGINE_CACHE_CATALOG_MAGIC ||
      read_le16(image + 4u) != PAL_ENGINE_CACHE_CATALOG_VERSION ||
      read_le16(image + 6u) != PAL_ENGINE_CACHE_CATALOG_HEADER_BYTES ||
      read_le32(image + PAL_CATALOG_SET_ID_OFFSET) == 0u ||
      read_le16(image + PAL_CATALOG_SCENE_COUNT_OFFSET) !=
         PAL_ENGINE_CACHE_CATALOG_SCENES ||
      read_le32(image + PAL_CATALOG_SCENE_TABLE_OFFSET) !=
         PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET ||
      read_le32(image + PAL_CATALOG_DESCRIPTOR_TABLE_OFFSET) !=
         PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET)
   {
      return false;
   }

   bundle_count = read_le16(image + PAL_CATALOG_BUNDLE_COUNT_OFFSET);
   if (bundle_count == 0u || bundle_count > PAL_ENGINE_CACHE_MAX_BUNDLES)
   {
      return false;
   }
   expected_total = PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET +
      (uint32_t)bundle_count * PAL_ENGINE_CACHE_DESCRIPTOR_BYTES;
   if (read_le32(image + PAL_CATALOG_TOTAL_BYTES_OFFSET) != expected_total ||
      expected_total != image_size ||
      !checked_range(PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET,
         PAL_ENGINE_CACHE_CATALOG_SCENES, image_size) ||
      !checked_range(PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET,
         (uint32_t)bundle_count * PAL_ENGINE_CACHE_DESCRIPTOR_BYTES,
         image_size))
   {
      return false;
   }
   declared_crc = read_le32(image + PAL_CATALOG_CRC32_OFFSET);
   if (declared_crc == 0u ||
      PalEngineChapterCache_Crc32(image, image_size,
         PAL_CATALOG_CRC32_OFFSET, 4u) != declared_crc)
   {
      return false;
   }

   for (i = 0; i < bundle_count; i++)
   {
      const uint8_t *descriptor = image +
         PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET +
         (uint32_t)i * PAL_ENGINE_CACHE_DESCRIPTOR_BYTES;
      uint8_t id = descriptor[PAL_DESCRIPTOR_ID_OFFSET];
      uint32_t size = read_le32(descriptor + PAL_DESCRIPTOR_SIZE_OFFSET);
      uint16_t j;
      bool nonzero_hash = false;

      if (id >= PAL_ENGINE_CACHE_MAX_BUNDLES ||
         descriptor[1] != 0u || descriptor[2] != 0u ||
         descriptor[3] != 0u ||
         size == 0u || size > PAL_ENGINE_CACHE_PACK_SOFT_BYTES)
      {
         return false;
      }
      for (j = 0; j < 32u; j++)
      {
         nonzero_hash = nonzero_hash ||
            descriptor[PAL_DESCRIPTOR_SHA256_OFFSET + j] != 0u;
      }
      if (!nonzero_hash)
      {
         return false;
      }
      for (j = 0; j < i; j++)
      {
         if (image[PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET +
               (uint32_t)j * PAL_ENGINE_CACHE_DESCRIPTOR_BYTES] == id)
         {
            return false;
         }
      }
   }
   if (image[PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET] != UINT8_MAX)
   {
      return false;
   }
   for (i = 1; i < PAL_ENGINE_CACHE_CATALOG_SCENES; i++)
   {
      uint8_t id = image[PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET + i];
      uint16_t j;
      bool found = false;

      for (j = 0; j < bundle_count; j++)
      {
         if (image[PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET +
               (uint32_t)j * PAL_ENGINE_CACHE_DESCRIPTOR_BYTES] == id)
         {
            found = true;
            break;
         }
      }
      if (!found)
      {
         return false;
      }
   }

   catalog->image = image;
   catalog->image_size = image_size;
   catalog->set_id = read_le32(image + PAL_CATALOG_SET_ID_OFFSET);
   catalog->scene_count = PAL_ENGINE_CACHE_CATALOG_SCENES;
   catalog->bundle_count = bundle_count;
   catalog->crc32 = declared_crc;
   return true;
}

bool
PalEngineChapterCache_DescribeScene(
   const PalEngineChapterCatalog *catalog,
   uint16_t scene,
   PalEngineChapterDescriptor *descriptor
)
{
   uint8_t id;
   uint16_t i;

   if (catalog == NULL || catalog->image == NULL || descriptor == NULL ||
      scene == 0u || scene >= catalog->scene_count)
   {
      return false;
   }
   id = catalog->image[PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET + scene];
   for (i = 0; i < catalog->bundle_count; i++)
   {
      const uint8_t *source = catalog_descriptor_at(catalog, i);
      if (source[PAL_DESCRIPTOR_ID_OFFSET] == id)
      {
         descriptor->bundle_id = id;
         descriptor->pack_size =
            read_le32(source + PAL_DESCRIPTOR_SIZE_OFFSET);
         memcpy(descriptor->sha256,
            source + PAL_DESCRIPTOR_SHA256_OFFSET,
            sizeof(descriptor->sha256));
         return true;
      }
   }
   return false;
}

bool
PalEngineChapterCache_PayloadMatches(
   const PalEngineChapterDescriptor *descriptor,
   const uint8_t *payload,
   uint32_t payload_size
)
{
   uint8_t digest[32];
   uint8_t difference = 0u;
   unsigned i;

   if (descriptor == NULL || payload == NULL ||
      payload_size != descriptor->pack_size)
   {
      return false;
   }
   PalEngineChapterCache_Sha256(payload, payload_size, digest);
   for (i = 0; i < sizeof(digest); i++)
   {
      difference |= (uint8_t)(digest[i] ^ descriptor->sha256[i]);
   }
   return difference == 0u;
}

bool
PalEngineChapterCache_BuildCommit(
   uint8_t *header,
   uint32_t header_capacity,
   uint32_t set_id,
   const PalEngineChapterDescriptor *descriptor,
   uint32_t generation,
   uint32_t catalog_crc32
)
{
   uint32_t crc;

   if (header == NULL || header_capacity < PAL_ENGINE_CACHE_COMMIT_BYTES ||
      descriptor == NULL || set_id == 0u || catalog_crc32 == 0u ||
      descriptor->bundle_id >= PAL_ENGINE_CACHE_MAX_BUNDLES ||
      descriptor->pack_size == 0u ||
      descriptor->pack_size > PAL_ENGINE_CACHE_PACK_SOFT_BYTES)
   {
      return false;
   }
   memset(header, 0, PAL_ENGINE_CACHE_COMMIT_BYTES);
   write_le32(header, PAL_ENGINE_CACHE_COMMIT_MAGIC);
   write_le16(header + 4u, PAL_ENGINE_CACHE_COMMIT_VERSION);
   write_le16(header + 6u, PAL_ENGINE_CACHE_COMMIT_BYTES);
   write_le32(header + PAL_COMMIT_STATE_OFFSET,
      PAL_ENGINE_CACHE_COMMIT_VALID);
   write_le32(header + PAL_COMMIT_SET_ID_OFFSET, set_id);
   header[PAL_COMMIT_BUNDLE_ID_OFFSET] = descriptor->bundle_id;
   write_le32(header + PAL_COMMIT_PAYLOAD_SIZE_OFFSET,
      descriptor->pack_size);
   memcpy(header + PAL_COMMIT_SHA256_OFFSET,
      descriptor->sha256, sizeof(descriptor->sha256));
   write_le32(header + PAL_COMMIT_CATALOG_CRC32_OFFSET, catalog_crc32);
   write_le32(header + PAL_COMMIT_GENERATION_OFFSET, generation);
   crc = PalEngineChapterCache_Crc32(header,
      PAL_ENGINE_CACHE_COMMIT_BYTES, PAL_COMMIT_CRC32_OFFSET, 4u);
   write_le32(header + PAL_COMMIT_CRC32_OFFSET, crc);
   return true;
}

PalEngineChapterCacheDecision
PalEngineChapterCache_Decide(
   const uint8_t *header,
   uint32_t header_size,
   uint32_t set_id,
   const PalEngineChapterDescriptor *descriptor,
   uint32_t catalog_crc32,
   uint32_t *generation
)
{
   uint32_t declared_crc;

   if (generation != NULL)
   {
      *generation = 0u;
   }
   if (header == NULL || header_size < PAL_ENGINE_CACHE_COMMIT_BYTES ||
      descriptor == NULL ||
      read_le32(header) != PAL_ENGINE_CACHE_COMMIT_MAGIC ||
      read_le16(header + 4u) != PAL_ENGINE_CACHE_COMMIT_VERSION ||
      read_le16(header + 6u) != PAL_ENGINE_CACHE_COMMIT_BYTES ||
      read_le32(header + PAL_COMMIT_STATE_OFFSET) !=
         PAL_ENGINE_CACHE_COMMIT_VALID ||
      read_le32(header + PAL_COMMIT_SET_ID_OFFSET) != set_id ||
      header[PAL_COMMIT_BUNDLE_ID_OFFSET] != descriptor->bundle_id ||
      header[17] != 0u || header[18] != 0u || header[19] != 0u ||
      read_le32(header + PAL_COMMIT_PAYLOAD_SIZE_OFFSET) !=
         descriptor->pack_size ||
      memcmp(header + PAL_COMMIT_SHA256_OFFSET,
         descriptor->sha256, sizeof(descriptor->sha256)) != 0 ||
      read_le32(header + PAL_COMMIT_CATALOG_CRC32_OFFSET) != catalog_crc32)
   {
      return PAL_ENGINE_CHAPTER_CACHE_REBUILD;
   }
   declared_crc = read_le32(header + PAL_COMMIT_CRC32_OFFSET);
   if (declared_crc == 0u ||
      PalEngineChapterCache_Crc32(header,
         PAL_ENGINE_CACHE_COMMIT_BYTES,
         PAL_COMMIT_CRC32_OFFSET, 4u) != declared_crc)
   {
      return PAL_ENGINE_CHAPTER_CACHE_REBUILD;
   }
   if (generation != NULL)
   {
      *generation = read_le32(header + PAL_COMMIT_GENERATION_OFFSET);
   }
   return PAL_ENGINE_CHAPTER_CACHE_VERIFY_PAYLOAD;
}

#if defined(PAL_CARDPUTER_EXTREME) && \
   defined(PAL_EXTREME_CHAPTER_CACHE) && defined(ESP_PLATFORM)

#include "../main/cardputer_extreme_audio.h"
#include "../main/cardputer_extreme_board.h"
#include "../main/cardputer_extreme_memory.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <ff.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define PAL_CACHE_PARTITION_SUBTYPE 0x41u
#define PAL_CACHE_PARTITION_LABEL "pal_cache"
#define PAL_CACHE_BUNDLE_PATH_BYTES 11u
#define PAL_CACHE_ERASE_STEP_BYTES 0x10000u
#define PAL_CACHE_FIRST_ERASE_BYTES \
   (PAL_CACHE_ERASE_STEP_BYTES - PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES)

typedef struct PalEngineChapterRuntime {
   PalEngineChapterCatalog catalog;
   const esp_partition_t *partition;
   const uint8_t *mapped_payload;
   esp_partition_mmap_handle_t mmap_handle;
   PalEngineChapterOverlayChanged overlay_changed;
   void *overlay_user;
   uint32_t generation;
   uint8_t current_bundle;
   bool mapped;
   bool enabled;
} PalEngineChapterRuntime;

static const char *TAG = "pal_chapter_cache";
static PalEngineChapterRuntime pal_chapter_runtime;
static uint8_t pal_sram_chapter_commit_expected[
   PAL_ENGINE_CACHE_COMMIT_BYTES];
static FIL pal_chapter_bundle_file;
static bool pal_chapter_bundle_open;

static bool
digest_equal(
   const uint8_t a[32],
   const uint8_t b[32]
)
{
   uint8_t difference = 0u;
   unsigned i;

   for (i = 0; i < 32u; i++)
   {
      difference |= (uint8_t)(a[i] ^ b[i]);
   }
   return difference == 0u;
}

static bool
open_bundle_file(
   const PalEngineChapterDescriptor *descriptor,
   char path[PAL_CACHE_BUNDLE_PATH_BYTES]
)
{
   static const char path_template[PAL_CACHE_BUNDLE_PATH_BYTES] =
      "0:/b00.pak";

   if (descriptor->bundle_id > 99u || pal_chapter_bundle_open)
   {
      return false;
   }
   memcpy(path, path_template, sizeof(path_template));
   path[4] = (char)('0' + descriptor->bundle_id / 10u);
   path[5] = (char)('0' + descriptor->bundle_id % 10u);
   if (f_open(&pal_chapter_bundle_file, path, FA_READ) != FR_OK)
   {
      return false;
   }
   pal_chapter_bundle_open = true;
   if ((uint64_t)f_size(&pal_chapter_bundle_file) !=
      descriptor->pack_size)
   {
      (void)f_close(&pal_chapter_bundle_file);
      pal_chapter_bundle_open = false;
      return false;
   }
   return true;
}

static bool
close_bundle_file(
   bool success
)
{
   if (pal_chapter_bundle_open)
   {
      success = f_close(&pal_chapter_bundle_file) == FR_OK &&
         success;
      pal_chapter_bundle_open = false;
   }
   return success;
}

static bool
detach_overlay(
   void
)
{
   if (!pal_chapter_runtime.mapped)
   {
      return true;
   }
   if (pal_chapter_runtime.overlay_changed != NULL &&
      !pal_chapter_runtime.overlay_changed(
         pal_chapter_runtime.overlay_user, NULL, 0u))
   {
      return false;
   }
   esp_partition_munmap(pal_chapter_runtime.mmap_handle);
   pal_chapter_runtime.mapped_payload = NULL;
   pal_chapter_runtime.mmap_handle = 0;
   pal_chapter_runtime.mapped = false;
   pal_chapter_runtime.current_bundle = UINT8_MAX;
   return true;
}

static bool
map_overlay(
   const PalEngineChapterDescriptor *descriptor,
   bool publish
)
{
   const void *mapped = NULL;
   esp_err_t err;

   err = esp_partition_mmap(pal_chapter_runtime.partition,
      PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES,
      descriptor->pack_size,
      ESP_PARTITION_MMAP_DATA,
      &mapped,
      &pal_chapter_runtime.mmap_handle);
   if (err != ESP_OK)
   {
      ESP_LOGE(TAG, "cache mmap: %s", esp_err_to_name(err));
      return false;
   }
   pal_chapter_runtime.mapped_payload = (const uint8_t *)mapped;
   pal_chapter_runtime.mapped = true;
   pal_chapter_runtime.current_bundle = descriptor->bundle_id;
   if (publish && pal_chapter_runtime.overlay_changed != NULL &&
      !pal_chapter_runtime.overlay_changed(
         pal_chapter_runtime.overlay_user,
         pal_chapter_runtime.mapped_payload,
         descriptor->pack_size))
   {
      esp_partition_munmap(pal_chapter_runtime.mmap_handle);
      pal_chapter_runtime.mapped_payload = NULL;
      pal_chapter_runtime.mmap_handle = 0;
      pal_chapter_runtime.mapped = false;
      pal_chapter_runtime.current_bundle = UINT8_MAX;
      (void)esp_partition_erase_range(pal_chapter_runtime.partition, 0u,
         PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES);
      ESP_LOGE(TAG, "cache overlay registration failed");
      return false;
   }
   return true;
}

static bool
publish_mapped_overlay(
   const PalEngineChapterDescriptor *descriptor
)
{
   if (pal_chapter_runtime.overlay_changed == NULL)
   {
      return true;
   }
   if (!pal_chapter_runtime.overlay_changed(
         pal_chapter_runtime.overlay_user,
         pal_chapter_runtime.mapped_payload,
         descriptor->pack_size))
   {
      esp_partition_munmap(pal_chapter_runtime.mmap_handle);
      pal_chapter_runtime.mapped_payload = NULL;
      pal_chapter_runtime.mmap_handle = 0;
      pal_chapter_runtime.mapped = false;
      pal_chapter_runtime.current_bundle = UINT8_MAX;
      (void)esp_partition_erase_range(pal_chapter_runtime.partition, 0u,
         PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES);
      ESP_LOGE(TAG, "cache overlay registration failed");
      return false;
   }
   return true;
}

static void
loading_progress(
   uint32_t percent
)
{
   if (percent > 100u)
   {
      percent = 100u;
   }
   CardputerExtreme_ShowLoading((uint8_t)percent);
}

static bool
show_cache_failure(
   void
)
{
   CardputerExtreme_ShowError("CACHE FAIL", "CHECK TF / RESET");
   return false;
}

static bool
pause_music(
   bool *paused
)
{
   *paused = false;
#if defined(PAL_EXTREME_RIX_MUSIC)
   if (CardputerExtremeAudio_Started())
   {
      unsigned attempt;

      if (!CardputerExtremeAudio_SetPaused(true))
      {
         ESP_LOGE(TAG, "cannot pause music for flash programming");
         return false;
      }
      /*
       * This flag means that a PAUSE command was accepted and therefore a
       * matching RESUME must be sent on every exit path.  The PAUSE may still
       * be in flight if the acknowledgement wait below times out.
       */
      *paused = true;
      for (attempt = 0; attempt < 100u; attempt++)
      {
         CardputerExtremeAudioTelemetry telemetry;
         CardputerExtremeAudio_GetTelemetry(&telemetry);
         if (telemetry.paused)
         {
            return true;
         }
         vTaskDelay(pdMS_TO_TICKS(1));
      }
      ESP_LOGE(TAG, "music pause timed out");
      return false;
   }
#endif
   return true;
}

static void
resume_music(
   bool paused
)
{
#if defined(PAL_EXTREME_RIX_MUSIC)
   if (paused)
   {
      unsigned attempt;
      bool queued = false;

      for (attempt = 0; attempt < 100u && !queued; attempt++)
      {
         queued = CardputerExtremeAudio_SetPaused(false);
         if (!queued)
         {
            vTaskDelay(pdMS_TO_TICKS(1));
         }
      }
      if (!queued)
      {
         ESP_LOGE(TAG, "cannot resume music after cache programming");
      }
   }
#else
   (void)paused;
#endif
}

static bool
hash_mapped_payload(
   const PalEngineChapterDescriptor *descriptor
)
{
   PalChapterSha256 sha;
   uint8_t digest[32];

   if (!pal_chapter_runtime.mapped ||
      pal_chapter_runtime.mapped_payload == NULL)
   {
      return false;
   }
   sha256_init(&sha);
   sha256_update(&sha, pal_chapter_runtime.mapped_payload,
      descriptor->pack_size);
   sha256_finish(&sha, digest);
   return digest_equal(digest, descriptor->sha256);
}

static bool
erase_payload(
   uint32_t payload_size
)
{
   uint32_t erase_bytes =
      (payload_size + PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES - 1u) &
      ~(PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES - 1u);
   uint32_t offset = 0u;

   while (offset < erase_bytes)
   {
      uint32_t amount = erase_bytes - offset;

      /*
       * The payload begins one 4 KiB sector past the 64 KiB-aligned
       * partition base.  Clear the first 15 sectors together, then issue
       * aligned 64 KiB ranges so the IDF can use block erase instead of
       * degrading the entire bundle to hundreds of sector erases.
       */
      if (offset == 0u && amount > PAL_CACHE_FIRST_ERASE_BYTES)
      {
         amount = PAL_CACHE_FIRST_ERASE_BYTES;
      }
      else if (offset != 0u && amount > PAL_CACHE_ERASE_STEP_BYTES)
      {
         amount = PAL_CACHE_ERASE_STEP_BYTES;
      }
      if (esp_partition_erase_range(pal_chapter_runtime.partition,
            PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES + offset,
            amount) != ESP_OK)
      {
         return false;
      }
      offset += amount;
      loading_progress(11u + (14u * offset) / erase_bytes);
      vTaskDelay(pdMS_TO_TICKS(1));
   }
   return true;
}

static bool
verify_bundle_on_tf(
   const PalEngineChapterDescriptor *descriptor
)
{
   PalChapterSha256 sha;
   uint8_t digest[32];
   char path[PAL_CACHE_BUNDLE_PATH_BYTES];
   uint32_t offset = 0u;
   bool success = false;

   if (!open_bundle_file(descriptor, path))
   {
      return false;
   }

   sha256_init(&sha);
   while (offset < descriptor->pack_size)
   {
      uint32_t amount = descriptor->pack_size - offset;
      UINT bytes_read = 0u;

      if (amount > PAL_EXTREME_DISPLAY_DMA_BYTES)
      {
         amount = PAL_EXTREME_DISPLAY_DMA_BYTES;
      }
      if (f_read(&pal_chapter_bundle_file,
            pal_sram_display_dma, amount, &bytes_read) != FR_OK ||
         bytes_read != amount)
      {
         goto done;
      }
      sha256_update(&sha, pal_sram_display_dma, amount);
      offset += amount;
      loading_progress((10u * offset) / descriptor->pack_size);
      if ((offset & (PAL_CACHE_ERASE_STEP_BYTES - 1u)) == 0u ||
         offset == descriptor->pack_size)
      {
         vTaskDelay(pdMS_TO_TICKS(1));
      }
   }
   sha256_finish(&sha, digest);
   success = digest_equal(digest, descriptor->sha256);

done:
   return close_bundle_file(success);
}

static bool
copy_bundle_from_tf(
   const PalEngineChapterDescriptor *descriptor
)
{
   PalChapterSha256 sha;
   uint8_t digest[32];
   char path[PAL_CACHE_BUNDLE_PATH_BYTES];
   uint32_t offset = 0u;
   bool success = false;

   if (!open_bundle_file(descriptor, path))
   {
      return false;
   }

   sha256_init(&sha);
   while (offset < descriptor->pack_size)
   {
      uint32_t amount = descriptor->pack_size - offset;
      UINT bytes_read = 0u;

      if (amount > PAL_EXTREME_DISPLAY_DMA_BYTES)
      {
         amount = PAL_EXTREME_DISPLAY_DMA_BYTES;
      }
      if (f_read(&pal_chapter_bundle_file,
            pal_sram_display_dma, amount, &bytes_read) != FR_OK ||
         bytes_read != amount)
      {
         goto done;
      }
      sha256_update(&sha, pal_sram_display_dma, amount);
      if (esp_partition_write(pal_chapter_runtime.partition,
            PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES + offset,
            pal_sram_display_dma, amount) != ESP_OK)
      {
         goto done;
      }
      offset += amount;
      loading_progress(25u +
         (55u * offset) / descriptor->pack_size);
      if ((offset & (PAL_CACHE_ERASE_STEP_BYTES - 1u)) == 0u ||
         offset == descriptor->pack_size)
      {
         vTaskDelay(pdMS_TO_TICKS(1));
      }
   }
   sha256_finish(&sha, digest);
   success = digest_equal(digest, descriptor->sha256);

done:
   return close_bundle_file(success);
}

static bool
verify_partition_readback(
   const PalEngineChapterDescriptor *descriptor
)
{
   PalChapterSha256 sha;
   uint8_t digest[32];
   uint32_t offset = 0u;

   sha256_init(&sha);
   while (offset < descriptor->pack_size)
   {
      uint32_t amount = descriptor->pack_size - offset;
      if (amount > PAL_EXTREME_DISPLAY_DMA_BYTES)
      {
         amount = PAL_EXTREME_DISPLAY_DMA_BYTES;
      }
      if (esp_partition_read(pal_chapter_runtime.partition,
            PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES + offset,
            pal_sram_display_dma, amount) != ESP_OK)
      {
         return false;
      }
      sha256_update(&sha, pal_sram_display_dma, amount);
      offset += amount;
      loading_progress(80u +
         (19u * offset) / descriptor->pack_size);
      if ((offset & (PAL_CACHE_ERASE_STEP_BYTES - 1u)) == 0u ||
         offset == descriptor->pack_size)
      {
         vTaskDelay(pdMS_TO_TICKS(1));
      }
   }
   sha256_finish(&sha, digest);
   return digest_equal(digest, descriptor->sha256);
}

static bool
write_commit_last(
   const PalEngineChapterDescriptor *descriptor
)
{
   uint32_t committed_generation = 0u;

   if (!PalEngineChapterCache_BuildCommit(
         pal_sram_chapter_commit_expected,
         sizeof(pal_sram_chapter_commit_expected),
         pal_chapter_runtime.catalog.set_id,
         descriptor,
         pal_chapter_runtime.generation,
         pal_chapter_runtime.catalog.crc32))
   {
      return false;
   }

   /*
    * Persist and read back every final field before programming VALID.
    * The CRC is already the CRC of the final header, so this staged image is
    * intentionally invalid while bytes 8..11 remain erased (0xff).
    */
   if (esp_partition_write(pal_chapter_runtime.partition, 0u,
         pal_sram_chapter_commit_expected,
         PAL_COMMIT_STATE_OFFSET) != ESP_OK ||
      esp_partition_write(pal_chapter_runtime.partition,
         PAL_COMMIT_STATE_OFFSET + 4u,
         pal_sram_chapter_commit_expected +
            PAL_COMMIT_STATE_OFFSET + 4u,
         PAL_ENGINE_CACHE_COMMIT_BYTES -
            PAL_COMMIT_STATE_OFFSET - 4u) != ESP_OK ||
      esp_partition_read(pal_chapter_runtime.partition, 0u,
         pal_sram_display_dma,
         PAL_ENGINE_CACHE_COMMIT_BYTES) != ESP_OK ||
      memcmp(pal_sram_display_dma,
         pal_sram_chapter_commit_expected,
         PAL_COMMIT_STATE_OFFSET) != 0 ||
      read_le32(pal_sram_display_dma + PAL_COMMIT_STATE_OFFSET) !=
         UINT32_MAX ||
      memcmp(pal_sram_display_dma + PAL_COMMIT_STATE_OFFSET + 4u,
         pal_sram_chapter_commit_expected +
            PAL_COMMIT_STATE_OFFSET + 4u,
         PAL_ENGINE_CACHE_COMMIT_BYTES -
            PAL_COMMIT_STATE_OFFSET - 4u) != 0)
   {
      return false;
   }

   /*
    * This aligned four-byte program is the sole commit point.  A torn marker
    * cannot equal PAL_ENGINE_CACHE_COMMIT_VALID unless every required 1->0
    * transition completed; all other bytes were already verified above.
    */
   if (esp_partition_write(pal_chapter_runtime.partition,
         PAL_COMMIT_STATE_OFFSET,
         pal_sram_chapter_commit_expected + PAL_COMMIT_STATE_OFFSET,
         4u) != ESP_OK ||
      esp_partition_read(pal_chapter_runtime.partition, 0u,
         pal_sram_display_dma,
         PAL_ENGINE_CACHE_COMMIT_BYTES) != ESP_OK ||
      PalEngineChapterCache_Decide(
         pal_sram_display_dma,
         PAL_ENGINE_CACHE_COMMIT_BYTES,
         pal_chapter_runtime.catalog.set_id,
         descriptor,
         pal_chapter_runtime.catalog.crc32,
         &committed_generation) !=
            PAL_ENGINE_CHAPTER_CACHE_VERIFY_PAYLOAD ||
      committed_generation != pal_chapter_runtime.generation)
   {
      return false;
   }
   return true;
}

static bool
rebuild_cache(
   const PalEngineChapterDescriptor *descriptor
)
{
   bool paused = false;
   bool success = false;

   ESP_LOGI(TAG, "rebuilding bundle %u (%" PRIu32 " bytes)",
      (unsigned)descriptor->bundle_id, descriptor->pack_size);
   loading_progress(0u);
   if (!pause_music(&paused) || !verify_bundle_on_tf(descriptor) ||
      !detach_overlay())
   {
      goto done;
   }

   /*
    * The commit sector is erased before touching the payload.  A reset at
    * every subsequent instruction before write_commit_last() therefore
    * leaves no valid commit.  That helper writes and verifies every other
    * field before programming the four-byte VALID marker as the sole commit
    * point.
    */
   if (esp_partition_erase_range(pal_chapter_runtime.partition, 0u,
         PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES) != ESP_OK)
   {
      goto done;
   }
   loading_progress(11u);
   if (!erase_payload(descriptor->pack_size) ||
      !copy_bundle_from_tf(descriptor) ||
      !verify_partition_readback(descriptor) ||
      !map_overlay(descriptor, true))
   {
      goto done;
   }

   /*
    * The mapped provider validates the pack header, pack CRC, set ID and
    * archive non-overlap before the persistent commit becomes valid.
    */
   pal_chapter_runtime.generation++;
   if (pal_chapter_runtime.generation == 0u)
   {
      pal_chapter_runtime.generation = 1u;
   }
   if (!write_commit_last(descriptor))
   {
      (void)detach_overlay();
      (void)esp_partition_erase_range(pal_chapter_runtime.partition, 0u,
         PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES);
      goto done;
   }
   loading_progress(100u);
   ESP_LOGI(TAG, "bundle %u committed at generation %" PRIu32,
      (unsigned)descriptor->bundle_id, pal_chapter_runtime.generation);
   success = true;

done:
   resume_music(paused);
   if (!success)
   {
      ESP_LOGE(TAG, "bundle %u reconstruction failed",
         (unsigned)descriptor->bundle_id);
      (void)show_cache_failure();
   }
   return success;
}

bool
PalEngineChapterCache_TargetInit(
   const uint8_t *catalog_image,
   uint32_t catalog_size,
   uint32_t core_set_id,
   PalEngineChapterOverlayChanged overlay_changed,
   void *overlay_user
)
{
   PalEngineChapterCatalog catalog;
   const esp_partition_t *partition;

   memset(&pal_chapter_runtime, 0, sizeof(pal_chapter_runtime));
   pal_chapter_runtime.current_bundle = UINT8_MAX;
   if (!PalEngineChapterCache_OpenCatalog(&catalog,
         catalog_image, catalog_size) ||
      catalog.set_id != core_set_id)
   {
      ESP_LOGE(TAG, "invalid core chapter catalog");
      return false;
   }
   partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      PAL_CACHE_PARTITION_SUBTYPE, PAL_CACHE_PARTITION_LABEL);
   if (partition == NULL ||
      partition->size != PAL_ENGINE_CACHE_PARTITION_BYTES)
   {
      ESP_LOGE(TAG, "missing or mis-sized %s partition",
         PAL_CACHE_PARTITION_LABEL);
      return false;
   }
   pal_chapter_runtime.catalog = catalog;
   pal_chapter_runtime.partition = partition;
   pal_chapter_runtime.overlay_changed = overlay_changed;
   pal_chapter_runtime.overlay_user = overlay_user;
   pal_chapter_runtime.enabled = true;
   return true;
}

bool
PalEngineChapterCache_Enabled(
   void
)
{
   return pal_chapter_runtime.enabled;
}

bool
PalEngineChapterCache_SceneNeedsBundle(
   uint16_t scene
)
{
   PalEngineChapterDescriptor descriptor;

   return pal_chapter_runtime.enabled &&
      PalEngineChapterCache_DescribeScene(&pal_chapter_runtime.catalog,
         scene, &descriptor) &&
      (!pal_chapter_runtime.mapped ||
         pal_chapter_runtime.current_bundle != descriptor.bundle_id);
}

bool
PalEngineChapterCache_PrepareScene(
   uint16_t scene,
   bool force_payload_verify
)
{
   PalEngineChapterDescriptor descriptor;
   PalEngineChapterCacheDecision decision;
   uint32_t generation = 0u;
   bool mapping_was_current;

   if (!pal_chapter_runtime.enabled ||
      !PalEngineChapterCache_DescribeScene(&pal_chapter_runtime.catalog,
         scene, &descriptor))
   {
      return show_cache_failure();
   }
   mapping_was_current = pal_chapter_runtime.mapped &&
      pal_chapter_runtime.current_bundle == descriptor.bundle_id;
   if (mapping_was_current && !force_payload_verify)
   {
      return true;
   }

   if (esp_partition_read(pal_chapter_runtime.partition, 0u,
         pal_sram_display_dma, PAL_ENGINE_CACHE_COMMIT_BYTES) != ESP_OK)
   {
      return show_cache_failure();
   }
   decision = PalEngineChapterCache_Decide(
      pal_sram_display_dma,
      PAL_ENGINE_CACHE_COMMIT_BYTES,
      pal_chapter_runtime.catalog.set_id,
      &descriptor,
      pal_chapter_runtime.catalog.crc32,
      &generation);
   if (decision == PAL_ENGINE_CHAPTER_CACHE_VERIFY_PAYLOAD)
   {
      if (!mapping_was_current)
      {
         if (!detach_overlay() || !map_overlay(&descriptor, false))
         {
            return show_cache_failure();
         }
      }
      if (hash_mapped_payload(&descriptor))
      {
         pal_chapter_runtime.generation = generation;
         if (mapping_was_current ||
            publish_mapped_overlay(&descriptor))
         {
            ESP_LOGI(TAG, "bundle %u payload SHA-256 verified",
               (unsigned)descriptor.bundle_id);
            return true;
         }
         return show_cache_failure();
      }
      if (!detach_overlay())
      {
         return show_cache_failure();
      }
      ESP_LOGW(TAG, "bundle %u payload SHA-256 mismatch",
         (unsigned)descriptor.bundle_id);
   }
   else
   {
      ESP_LOGI(TAG, "bundle %u cache record requires rebuild",
         (unsigned)descriptor.bundle_id);
   }
   return rebuild_cache(&descriptor);
}

uint8_t
PalEngineChapterCache_CurrentBundle(
   void
)
{
   return pal_chapter_runtime.mapped
      ? pal_chapter_runtime.current_bundle : UINT8_MAX;
}

#else

bool
PalEngineChapterCache_TargetInit(
   const uint8_t *catalog_image,
   uint32_t catalog_size,
   uint32_t core_set_id,
   PalEngineChapterOverlayChanged overlay_changed,
   void *overlay_user
)
{
   (void)catalog_image;
   (void)catalog_size;
   (void)core_set_id;
   (void)overlay_changed;
   (void)overlay_user;
   return false;
}

bool
PalEngineChapterCache_Enabled(
   void
)
{
   return false;
}

bool
PalEngineChapterCache_SceneNeedsBundle(
   uint16_t scene
)
{
   (void)scene;
   return false;
}

bool
PalEngineChapterCache_PrepareScene(
   uint16_t scene,
   bool force_payload_verify
)
{
   (void)scene;
   (void)force_payload_verify;
   return true;
}

uint8_t
PalEngineChapterCache_CurrentBundle(
   void
)
{
   return UINT8_MAX;
}

#endif
