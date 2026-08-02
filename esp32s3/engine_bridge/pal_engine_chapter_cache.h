#ifndef PAL_ENGINE_CHAPTER_CACHE_H
#define PAL_ENGINE_CHAPTER_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cardputer ADV 8 MiB flash layout.
 *
 * pal_cache sector zero is a commit record.  A bundle pack starts in the
 * following sector.  The 64 KiB difference between the hard and soft limits
 * is deliberate headroom for future pack-format growth.
 */
#define PAL_ENGINE_CACHE_PARTITION_BYTES 0x002d0000u
#define PAL_ENGINE_CACHE_COMMIT_SECTOR_BYTES 0x00001000u
#define PAL_ENGINE_CACHE_PAYLOAD_HARD_BYTES 0x002cf000u
#define PAL_ENGINE_CACHE_PACK_SOFT_BYTES 0x002bf000u

#define PAL_ENGINE_CACHE_CATALOG_MAGIC 0x43424c50u /* "PLBC" */
#define PAL_ENGINE_CACHE_CATALOG_VERSION 1u
#define PAL_ENGINE_CACHE_CATALOG_HEADER_BYTES 32u
#define PAL_ENGINE_CACHE_CATALOG_SCENES 300u
#define PAL_ENGINE_CACHE_CATALOG_SCENE_OFFSET 32u
#define PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET 332u
#define PAL_ENGINE_CACHE_DESCRIPTOR_BYTES 40u
#define PAL_ENGINE_CACHE_MAX_BUNDLES 100u

/*
 * PALSET.BIN is the bounded TF bootstrap record.  It deliberately owns the
 * data-set identity and hashes so the firmware image contains no generated
 * resource digest and can boot any compatible pack set.
 */
#define PAL_ENGINE_CACHE_SET_MAGIC 0x54534c50u /* "PLST" */
#define PAL_ENGINE_CACHE_SET_VERSION 1u
#define PAL_ENGINE_CACHE_SET_HEADER_BYTES 64u
#define PAL_ENGINE_CACHE_SET_CRC32_OFFSET 28u
#define PAL_ENGINE_CACHE_SET_CORE_SHA256_OFFSET 32u
#define PAL_ENGINE_CACHE_SET_MAX_BYTES \
   (PAL_ENGINE_CACHE_SET_HEADER_BYTES + \
      PAL_ENGINE_CACHE_CATALOG_DESCRIPTOR_OFFSET + \
      PAL_ENGINE_CACHE_MAX_BUNDLES * PAL_ENGINE_CACHE_DESCRIPTOR_BYTES)

#define PAL_ENGINE_CORE_PARTITION_BYTES 0x00460000u

#define PAL_ENGINE_CACHE_COMMIT_MAGIC 0x48434c50u /* "PLCH" */
#define PAL_ENGINE_CACHE_COMMIT_VERSION 1u
#define PAL_ENGINE_CACHE_COMMIT_BYTES 80u
#define PAL_ENGINE_CACHE_COMMIT_VALID 0x44494c56u /* "VLID" */

typedef struct PalEngineChapterCatalog {
   const uint8_t *image;
   uint32_t image_size;
   uint32_t set_id;
   uint16_t scene_count;
   uint16_t bundle_count;
   uint32_t crc32;
} PalEngineChapterCatalog;

typedef struct PalEngineChapterDescriptor {
   uint8_t bundle_id;
   uint32_t pack_size;
   uint8_t sha256[32];
} PalEngineChapterDescriptor;

typedef struct PalEngineChapterSet {
   const uint8_t *image;
   uint32_t image_size;
   uint32_t set_id;
   uint32_t core_size;
   const uint8_t *core_sha256;
   PalEngineChapterCatalog catalog;
   uint32_t crc32;
} PalEngineChapterSet;

typedef enum PalEngineChapterCacheDecision {
   PAL_ENGINE_CHAPTER_CACHE_REBUILD = 0,
   PAL_ENGINE_CHAPTER_CACHE_VERIFY_PAYLOAD = 1
} PalEngineChapterCacheDecision;

/*
 * Pure-C format and decision helpers.  These have no ESP-IDF dependency and
 * are kept public so the host unit test validates exactly the target format.
 */
uint32_t PalEngineChapterCache_Crc32(
   const uint8_t *bytes,
   uint32_t size,
   uint32_t zero_offset,
   uint32_t zero_size);
void PalEngineChapterCache_Sha256(
   const uint8_t *bytes,
   uint32_t size,
   uint8_t digest[32]);
bool PalEngineChapterCache_OpenCatalog(
   PalEngineChapterCatalog *catalog,
   const uint8_t *image,
   uint32_t image_size);
bool PalEngineChapterCache_OpenSet(
   PalEngineChapterSet *set,
   const uint8_t *image,
   uint32_t image_size);
bool PalEngineChapterCache_DescribeScene(
   const PalEngineChapterCatalog *catalog,
   uint16_t scene,
   PalEngineChapterDescriptor *descriptor);
bool PalEngineChapterCache_PayloadMatches(
   const PalEngineChapterDescriptor *descriptor,
   const uint8_t *payload,
   uint32_t payload_size);
bool PalEngineChapterCache_BuildCommit(
   uint8_t *header,
   uint32_t header_capacity,
   uint32_t set_id,
   const PalEngineChapterDescriptor *descriptor,
   uint32_t generation,
   uint32_t catalog_crc32);
PalEngineChapterCacheDecision PalEngineChapterCache_Decide(
   const uint8_t *header,
   uint32_t header_size,
   uint32_t set_id,
   const PalEngineChapterDescriptor *descriptor,
   uint32_t catalog_crc32,
   uint32_t *generation);

/*
 * The overlay callback is invoked with NULL before the mapped cache is
 * invalidated, and with the verified bundle image after it is mapped again.
 * Returning false for registration leaves the cache unadvertised.
 */
typedef bool (*PalEngineChapterOverlayChanged)(
   void *user,
   const uint8_t *image,
   uint32_t image_size);

/* Mount TF first, then call this before reading or mapping pal_core. */
bool PalEngineChapterCache_TargetPrepareCore(
   const uint8_t **catalog_image,
   uint32_t *catalog_size,
   uint32_t *set_id);

bool PalEngineChapterCache_TargetInit(
   const uint8_t *catalog_image,
   uint32_t catalog_size,
   uint32_t core_set_id,
   PalEngineChapterOverlayChanged overlay_changed,
   void *overlay_user);
bool PalEngineChapterCache_Enabled(void);
bool PalEngineChapterCache_SceneNeedsBundle(uint16_t scene);
bool PalEngineChapterCache_PrepareScene(
   uint16_t scene,
   bool force_payload_verify);
uint8_t PalEngineChapterCache_CurrentBundle(void);

#ifdef __cplusplus
}
#endif

#endif
